#include "world/World.h"

#include "render/ArchitecturalFixtureMesh.h"
#include "render/BackroomsPropMesh.h"
#include "render/VisualVegetation.h"
#include "render/VisualVegetationMesh.h"

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
// Je couvre largement le rayon maximal des arbres proceduraux actuels. Le
// composant et son feuillage sont ainsi classes de la meme facon des deux
// cotes d'une frontiere de chunk, y compris aux coordonnees negatives.
constexpr int kCanonicalVisualVegetationHalo = 8;
constexpr auto kMeshInvalidationPriorityRadius = 2;
constexpr std::size_t kLightingTimeCheckInterval = 128;
constexpr int kBackroomsVerticalLightStride = 3;
constexpr int kPoolroomsVerticalLightStride = 4;
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

auto player_placed_mask_test(
    const WorldPlayerPlacedMask& mask,
    std::size_t block_index) noexcept -> bool {
    if (block_index >= kChunkVolume) {
        return false;
    }
    const auto byte_index = block_index / 8U;
    const auto bit_index = block_index % 8U;
    return (
               mask[byte_index] &
               static_cast<std::uint8_t>(
                   1U << bit_index)) !=
           0U;
}

void player_placed_mask_set(
    WorldPlayerPlacedMask& mask,
    std::size_t block_index,
    bool value) noexcept {
    if (block_index >= kChunkVolume) {
        return;
    }
    const auto byte_index = block_index / 8U;
    const auto bit =
        static_cast<std::uint8_t>(
            1U << (block_index % 8U));
    if (value) {
        mask[byte_index] =
            static_cast<std::uint8_t>(
                mask[byte_index] | bit);
    } else {
        mask[byte_index] =
            static_cast<std::uint8_t>(
                mask[byte_index] &
                static_cast<std::uint8_t>(~bit));
    }
}

auto player_placed_mask_count(
    const WorldPlayerPlacedMask& mask) noexcept -> std::size_t {
    auto count = std::size_t {0U};
    for (auto byte : mask) {
        while (byte != 0U) {
            byte =
                static_cast<std::uint8_t>(
                    byte &
                    static_cast<std::uint8_t>(
                        byte - 1U));
            ++count;
        }
    }
    return count;
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

[[nodiscard]] auto chunk_section_has_organic_surface(
    const World& world,
    const Chunk& chunk,
    std::size_t section_index) -> bool {

    const auto min_y = section_min_y(section_index);
    const auto max_y = section_max_y(section_index);
    if (max_y < chunk.min_mesh_y() || min_y > chunk.max_mesh_y()) {
        return false;
    }
    constexpr std::array<BlockCoord, 6> kSurfaceNeighbors {{
        {1, 0, 0},
        {-1, 0, 0},
        {0, 1, 0},
        {0, -1, 0},
        {0, 0, 1},
        {0, 0, -1},
    }};
    const auto coord = chunk.coord();
    const auto world_x = coord.x * kChunkSizeX;
    const auto world_z = coord.z * kChunkSizeZ;
    for (int y = min_y; y <= max_y; ++y) {
        for (int z = 0; z < kChunkSizeZ; ++z) {
            for (int x = 0; x < kChunkSizeX; ++x) {
                if (!is_organic_terrain_block(chunk.get_local(x, y, z))) {
                    continue;
                }
                for (const auto offset : kSurfaceNeighbors) {
                    const auto neighbor_x = x + offset.x;
                    const auto neighbor_y = y + offset.y;
                    const auto neighbor_z = z + offset.z;
                    const auto neighbor_inside_chunk =
                        neighbor_x >= 0 && neighbor_x < kChunkSizeX &&
                        neighbor_z >= 0 && neighbor_z < kChunkSizeZ &&
                        is_world_y_valid(neighbor_y);
                    const auto neighbor_block = neighbor_inside_chunk
                        ? chunk.get_local(neighbor_x, neighbor_y, neighbor_z)
                        : world.peek_block_or_generated(
                              world_x + neighbor_x,
                              neighbor_y,
                              world_z + neighbor_z);
                    if (!is_organic_terrain_block(neighbor_block)) {
                        return true;
                    }
                }
            }
        }
    }
    return false;
}

[[nodiscard]] auto chunk_section_has_architecture(
    const Chunk& chunk,
    std::size_t section_index) -> bool {

    const auto min_y = section_min_y(section_index);
    const auto max_y = section_max_y(section_index);
    if (max_y < chunk.min_mesh_y() || min_y > chunk.max_mesh_y()) {
        return false;
    }
    for (int y = min_y; y <= max_y; ++y) {
        for (int z = 0; z < kChunkSizeZ; ++z) {
            for (int x = 0; x < kChunkSizeX; ++x) {
                const auto block = chunk.get_local(x, y, z);
                if (is_architectural_solid_block(block) ||
                    is_architectural_fixture_block(block) ||
                    is_modern_backrooms_hard_surface_prop(block)) {
                    return true;
                }
            }
        }
    }
    return false;
}

[[nodiscard]] auto coordinate_is_tree_wood(
    const VisualVegetationBuild& vegetation,
    BlockCoord coordinate,
    BlockId block) noexcept -> bool {

    for (const auto& source : vegetation.sources) {
        const auto tree =
            source.kind == VisualVegetationSourceKind::BroadleafTree ||
            source.kind == VisualVegetationSourceKind::PineTree;
        if (!tree || source.source_block != block) {
            continue;
        }

        // Les arbres du generateur ont une colonne de tronc. Je limite le
        // masquage a cette colonne exacte pour ne jamais avaler une poutre
        // voisine qui se trouverait sous le feuillage.
        if (coordinate.x == source.anchor.x &&
            coordinate.z == source.anchor.z &&
            static_cast<float>(coordinate.y) >= source.logical_bounds.min_y &&
            static_cast<float>(coordinate.y + 1) <= source.logical_bounds.max_y) {
            return true;
        }
    }
    return false;
}

[[nodiscard]] auto is_visual_vegetation_source_block(BlockId block) noexcept
    -> bool {
    switch (static_cast<BlockType>(block)) {
    case BlockType::Wood:
    case BlockType::Leaves:
    case BlockType::PineWood:
    case BlockType::PineLeaves:
    case BlockType::TallGrass:
    case BlockType::RedFlower:
    case BlockType::YellowFlower:
    case BlockType::Cactus:
        return true;
    default:
        return false;
    }
}

[[nodiscard]] auto chunk_section_has_visual_vegetation(
    const Chunk& chunk,
    std::size_t section_index) -> bool {
    const auto min_y = section_min_y(section_index);
    const auto max_y = section_max_y(section_index);
    if (max_y < chunk.min_mesh_y() || min_y > chunk.max_mesh_y()) {
        return false;
    }
    for (int y = min_y; y <= max_y; ++y) {
        for (int z = 0; z < kChunkSizeZ; ++z) {
            for (int x = 0; x < kChunkSizeX; ++x) {
                if (is_visual_vegetation_source_block(
                        chunk.get_local(x, y, z))) {
                    return true;
                }
            }
        }
    }
    return false;
}

[[nodiscard]] auto chunk_has_visual_vegetation(
    const Chunk& chunk) -> bool {
    for (std::size_t section_index = 0U;
         section_index < kChunkSectionCount;
         ++section_index) {
        if (chunk_section_has_visual_vegetation(
                chunk,
                section_index)) {
            return true;
        }
    }
    return false;
}

void append_organic_mesh(
    OrganicTerrainMesh& destination,
    const OrganicTerrainMesh& source) {
    if (source.empty()) {
        return;
    }
    const auto vertex_offset =
        static_cast<std::uint32_t>(destination.vertices.size());
    destination.vertices.insert(
        destination.vertices.end(),
        source.vertices.begin(),
        source.vertices.end());
    destination.indices.reserve(
        destination.indices.size() + source.indices.size());
    for (const auto index : source.indices) {
        destination.indices.push_back(vertex_offset + index);
    }
    destination.quad_count += source.quad_count;
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

void append_architectural_mesh(
    ArchitecturalMesh& destination,
    const ArchitecturalMesh& source) {
    if (source.empty()) {
        return;
    }

    const auto vertex_offset =
        static_cast<std::uint32_t>(destination.vertices.size());
    const auto index_offset =
        static_cast<std::uint32_t>(destination.indices.size());
    destination.vertices.insert(
        destination.vertices.end(),
        source.vertices.begin(),
        source.vertices.end());
    destination.indices.reserve(
        destination.indices.size() + source.indices.size());
    for (const auto index : source.indices) {
        destination.indices.push_back(vertex_offset + index);
    }
    destination.quads.reserve(destination.quads.size() + source.quads.size());
    for (auto quad : source.quads) {
        quad.first_vertex += vertex_offset;
        quad.first_index += index_offset;
        destination.quads.push_back(quad);
    }
    destination.fixtures.insert(
        destination.fixtures.end(),
        source.fixtures.begin(),
        source.fixtures.end());

    if (!source.bounds.valid) {
        return;
    }
    if (!destination.bounds.valid) {
        destination.bounds = source.bounds;
        return;
    }
    destination.bounds.min_x =
        std::min(destination.bounds.min_x, source.bounds.min_x);
    destination.bounds.min_y =
        std::min(destination.bounds.min_y, source.bounds.min_y);
    destination.bounds.min_z =
        std::min(destination.bounds.min_z, source.bounds.min_z);
    destination.bounds.max_x =
        std::max(destination.bounds.max_x, source.bounds.max_x);
    destination.bounds.max_y =
        std::max(destination.bounds.max_y, source.bounds.max_y);
    destination.bounds.max_z =
        std::max(destination.bounds.max_z, source.bounds.max_z);
}

void mix_revision(std::uint64_t& hash, std::uint64_t value) noexcept {
    for (std::size_t byte_index = 0U; byte_index < 8U; ++byte_index) {
        hash ^= (value >> static_cast<unsigned int>(byte_index * 8U)) & 0xFFULL;
        hash *= 1099511628211ULL;
    }
}

} // namespace

World::World(
    int seed,
    int stream_radius,
    WorldGenerationProfile generation_profile,
    WorldGenerationVersion generation_version,
    VisualPipeline visual_pipeline)
    // Je conserve le symbole historique à cinq arguments pour que mes modules
    // déjà compilés restent compatibles, puis je centralise toute l'initialisation.
    : World(
          seed,
          stream_radius,
          generation_profile,
          generation_version,
          visual_pipeline,
          0) {}

World::World(int seed,
             int stream_radius,
             WorldGenerationProfile generation_profile,
             WorldGenerationVersion generation_version,
             VisualPipeline visual_pipeline,
             int backrooms_level)
    : stream_radius_(std::clamp(stream_radius, 0, kMaxStreamRadius)),
      generator_(
          seed,
          generation_profile,
          generation_version,
          backrooms_level),
      visual_pipeline_(visual_pipeline),
      active_stream_radius_(stream_radius_) {
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
    fluid_pressure_head_cache_.reserve(512U);
    fluid_pressure_head_missing_cache_.reserve(128U);
    fluid_pressure_frontier_.reserve(128U);
    fluid_pressure_visited_.reserve(128U);
    fluid_pressure_seen_.reserve(256U);
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
        const auto block_index = chunk_linear_index(local.x, local.y, local.z);
        const auto& entry = override_iterator->second;
        if (entry.dense != nullptr) {
            return entry.dense->blocks[block_index];
        }
        if (entry.changed_cells.test(block_index)) {
            const auto cell = find_sparse_override_cell(entry, block_index);
            if (cell != entry.sparse_cells.end()) {
                return cell->block;
            }
        }
        return generator_.sample_block(x, y, z);
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
        const auto block_index = chunk_linear_index(local.x, local.y, local.z);
        const auto& entry = override_iterator->second;
        if (entry.dense != nullptr) {
            return entry.dense->water_state[block_index];
        }
        if (entry.changed_cells.test(block_index)) {
            const auto cell = find_sparse_override_cell(entry, block_index);
            if (cell != entry.sparse_cells.end()) {
                return cell->water_state;
            }
        }
        return generator_.sample_water_state(x, y, z);
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
        const auto block_index = chunk_linear_index(local.x, local.y, local.z);
        const auto& entry = override_iterator->second;
        if (entry.dense != nullptr) {
            return entry.dense->blocks[block_index];
        }
        if (entry.changed_cells.test(block_index)) {
            const auto cell = find_sparse_override_cell(entry, block_index);
            if (cell != entry.sparse_cells.end()) {
                return cell->block;
            }
        }
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
        const auto block_index = chunk_linear_index(local.x, local.y, local.z);
        const auto& entry = override_iterator->second;
        if (entry.dense != nullptr) {
            return water_level_from_state(entry.dense->water_state[block_index]);
        }
        if (entry.changed_cells.test(block_index)) {
            const auto cell = find_sparse_override_cell(entry, block_index);
            if (cell != entry.sparse_cells.end()) {
                return water_level_from_state(cell->water_state);
            }
        }
    }

    return water_level_from_state(generator_.sample_water_state(x, y, z));
}

auto World::peek_column_or_generated(
    int x,
    int z) const -> WorldGeneratedColumn {
    WorldGeneratedColumn result {};
    const auto chunk_coord = world_to_chunk(x, z);
    const auto local = world_to_local(x, kWorldMinY, z);
    if (const auto* chunk = find_chunk(chunk_coord);
        chunk != nullptr) {
        const auto& blocks = chunk->blocks();
        const auto& water = chunk->water_state();
        for (int y = kWorldMinY; y <= kWorldMaxY; ++y) {
            const auto column_index = static_cast<std::size_t>(y);
            const auto chunk_index =
                chunk_linear_index(local.x, y, local.z);
            result.blocks[column_index] = blocks[chunk_index];
            result.water_state[column_index] = water[chunk_index];
        }
        return result;
    }

    // Je calcule le halo procédural une seule fois par colonne. Backrooms V2
    // mutualise ainsi son analyse des connexions pour les blocs et l'eau au
    // lieu de la répéter pour chaque face de bassin en bord de streaming.
    result = generator_.sample_generated_column(x, z);
    const auto override_iterator =
        chunk_overrides_.find(chunk_coord);
    if (override_iterator == chunk_overrides_.end()) {
        return result;
    }

    const auto& entry = override_iterator->second;
    if (entry.dense != nullptr) {
        for (int y = kWorldMinY; y <= kWorldMaxY; ++y) {
            const auto column_index = static_cast<std::size_t>(y);
            const auto chunk_index =
                chunk_linear_index(local.x, y, local.z);
            result.blocks[column_index] =
                entry.dense->blocks[chunk_index];
            result.water_state[column_index] =
                entry.dense->water_state[chunk_index];
        }
        return result;
    }

    for (const auto& cell : entry.sparse_cells) {
        const auto chunk_index =
            static_cast<std::size_t>(cell.index);
        const auto cell_x =
            static_cast<int>(
                chunk_index %
                static_cast<std::size_t>(kChunkSizeX));
        const auto yz_index =
            chunk_index /
            static_cast<std::size_t>(kChunkSizeX);
        const auto cell_z =
            static_cast<int>(
                yz_index %
                static_cast<std::size_t>(kChunkSizeZ));
        if (cell_x != local.x || cell_z != local.z) {
            continue;
        }
        const auto y =
            yz_index /
            static_cast<std::size_t>(kChunkSizeZ);
        if (y >= result.blocks.size()) {
            continue;
        }
        result.blocks[y] = cell.block;
        result.water_state[y] = cell.water_state;
    }
    return result;
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
    (void)set_block_internal(
        x,
        y,
        z,
        block_id,
        false);
}

auto World::set_player_block(
    int x,
    int y,
    int z,
    BlockId block_id) -> bool {
    return set_block_internal(
        x,
        y,
        z,
        block_id,
        true);
}

auto World::was_player_placed(
    int x,
    int y,
    int z) const noexcept -> bool {
    if (!is_world_y_valid(y)) {
        return false;
    }

    const auto chunk_coord =
        world_to_chunk(
            x,
            z);
    const auto override_iterator =
        chunk_overrides_.find(
            chunk_coord);
    if (override_iterator ==
        chunk_overrides_.end()) {
        return false;
    }

    const auto local =
        world_to_local(
            x,
            y,
            z);
    return player_placed_mask_test(
        override_iterator->second.player_placed_mask,
        chunk_linear_index(
            local.x,
            local.y,
            local.z));
}

auto World::capture_cell_snapshot(
    int x,
    int y,
    int z) const noexcept
    -> std::optional<WorldCellSnapshot> {
    if (!is_world_y_valid(y)) {
        return std::nullopt;
    }

    const auto chunk_coord =
        world_to_chunk(
            x,
            z);
    const auto* chunk =
        find_chunk(
            chunk_coord);
    if (chunk == nullptr) {
        return std::nullopt;
    }

    const auto local =
        world_to_local(
            x,
            y,
            z);
    return WorldCellSnapshot {
        {x, y, z},
        chunk->get_local(
            local.x,
            local.y,
            local.z),
        chunk->get_water_state_local(
            local.x,
            local.y,
            local.z),
        was_player_placed(
            x,
            y,
            z),
    };
}

auto World::restore_cell_snapshot(
    const WorldCellSnapshot& snapshot) -> bool {
    const auto& coordinate =
        snapshot.coordinate;
    if (!is_world_y_valid(
            coordinate.y) ||
        !is_known_block_id(
            snapshot.block)) {
        return false;
    }

    const auto chunk_coord =
        world_to_chunk(
            coordinate.x,
            coordinate.z);
    ensure_chunk_loaded(
        chunk_coord);
    auto iterator =
        chunks_.find(
            chunk_coord);
    if (iterator == chunks_.end()) {
        return false;
    }

    const auto local =
        world_to_local(
            coordinate.x,
            coordinate.y,
            coordinate.z);
    auto& record =
        iterator->second;
    auto& chunk =
        record.chunk;
    const auto current_block =
        chunk.get_local(
            local.x,
            local.y,
            local.z);
    const auto current_water_state =
        chunk.get_water_state_local(
            local.x,
            local.y,
            local.z);
    const auto current_player_placed =
        was_player_placed(
            coordinate.x,
            coordinate.y,
            coordinate.z);
    if (current_block == snapshot.block &&
        current_water_state ==
            snapshot.water_state &&
        current_player_placed ==
            snapshot.player_placed) {
        return true;
    }

    if (current_block != snapshot.block) {
        chunk.set_local(
            local.x,
            local.y,
            local.z,
            snapshot.block);
    }
    if (current_water_state !=
        snapshot.water_state) {
        chunk.set_water_state_local(
            local.x,
            local.y,
            local.z,
            snapshot.water_state);
    }

    const auto generated_block =
        generator_.sample_block(
            coordinate.x,
            coordinate.y,
            coordinate.z);
    const auto generated_water_state =
        generator_.sample_water_state(
            coordinate.x,
            coordinate.y,
            coordinate.z);
    auto override_iterator =
        chunk_overrides_.find(
            chunk_coord);
    if (override_iterator ==
        chunk_overrides_.end()) {
        const auto mismatch =
            snapshot.block !=
                generated_block ||
            snapshot.water_state !=
                generated_water_state;
        if (mismatch ||
            snapshot.player_placed) {
            ChunkOverrideEntry entry {};
            entry.sparse_cells.reserve(
                std::min<std::size_t>(
                    64U,
                    kSparseOverrideCellLimit));
            override_iterator =
                chunk_overrides_
                    .emplace(
                        chunk_coord,
                        std::move(entry))
                    .first;
        }
    }
    if (override_iterator !=
        chunk_overrides_.end()) {
        auto& entry =
            override_iterator->second;
        set_chunk_override_cell(
            entry,
            chunk_linear_index(
                local.x,
                local.y,
                local.z),
            snapshot.block,
            snapshot.water_state,
            generated_block,
            generated_water_state,
            snapshot.player_placed,
            &chunk);
        if (entry.generator_mismatch_count ==
                0U &&
            entry.player_placed_count == 0U) {
            chunk_overrides_.erase(
                override_iterator);
        }
    }

    const auto cell_changed =
        current_block != snapshot.block ||
        current_water_state !=
            snapshot.water_state;
    if (!cell_changed) {
        return true;
    }
    if (current_block != snapshot.block) {
        update_chunk_emissive_cache(
            record,
            local,
            current_block,
            snapshot.block);
        mark_sky_column_dirty(
            chunk_coord,
            local.x,
            local.z);
        mark_chunk_and_neighbors_lighting_dirty(
            chunk_coord);
    }
    mark_chunk_and_neighbors_dirty(
        chunk_coord,
        local);
    enqueue_fluid_cell(
        coordinate);
    enqueue_adjacent_fluid_cells(
        coordinate);
    return true;
}

auto World::set_block_internal(
    int x,
    int y,
    int z,
    BlockId block_id,
    bool mark_player_placed) -> bool {
    if (!is_world_y_valid(y)) {
        return false;
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
    const auto already_player_placed =
        was_player_placed(
            x,
            y,
            z);

    auto next_block = current_block;
    auto next_water_state = current_water_state;
    if (block_id == to_block_id(BlockType::Water)) {
        if (current_block != to_block_id(BlockType::Air) &&
            !is_block_replaceable(current_block) &&
            !is_torch_block(current_block)) {
            return false;
        }
        next_block = to_block_id(BlockType::Air);
        next_water_state = make_water_state(kMaxWaterLevel, true);
    } else {
        next_block = block_id;
        next_water_state = 0;
    }

    const auto cell_changed =
        current_block != next_block ||
        current_water_state != next_water_state;
    const auto provenance_changed =
        mark_player_placed &&
        !already_player_placed;
    if (!cell_changed &&
        !provenance_changed) {
        return false;
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
        next_water_state,
        mark_player_placed);

    if (!cell_changed) {
        return true;
    }

    if (current_block != next_block) {
        update_chunk_emissive_cache(record, local, current_block, next_block);
        mark_sky_column_dirty(chunk_coord, local.x, local.z);
        mark_chunk_and_neighbors_lighting_dirty(chunk_coord);
        remove_unsupported_torches_around(x, y, z);
    }

    mark_chunk_and_neighbors_dirty(chunk_coord, local);
    enqueue_fluid_cell({x, y, z});
    enqueue_adjacent_fluid_cells({x, y, z});
    return true;
}

auto World::restore_generated_cell(int x, int y, int z) -> bool {
    if (!is_world_y_valid(y)) {
        return false;
    }

    const auto chunk_coord = world_to_chunk(x, z);
    const auto local = world_to_local(x, y, z);
    auto iterator = chunks_.find(chunk_coord);
    if (iterator == chunks_.end()) {
        auto override_iterator = chunk_overrides_.find(chunk_coord);
        if (override_iterator == chunk_overrides_.end()) {
            return false;
        }

        auto& entry = override_iterator->second;
        const auto block_index = chunk_linear_index(local.x, local.y, local.z);
        auto generated_block = to_block_id(BlockType::Air);
        auto generated_water_state = WaterState {0};
        auto current_block = generated_block;
        auto current_water_state = generated_water_state;
        if (entry.dense != nullptr) {
            generated_block = entry.dense->generated_blocks[block_index];
            generated_water_state = entry.dense->generated_water_state[block_index];
            current_block = entry.dense->blocks[block_index];
            current_water_state = entry.dense->water_state[block_index];
        } else {
            const auto cell = find_sparse_override_cell(entry, block_index);
            if (cell == entry.sparse_cells.end() || static_cast<std::size_t>(cell->index) != block_index) {
                return false;
            }
            generated_block = cell->generated_block;
            generated_water_state = cell->generated_water_state;
            current_block = cell->block;
            current_water_state = cell->water_state;
        }

        if (current_block == generated_block && current_water_state == generated_water_state) {
            return false;
        }

        // Je retire directement l'override persistant. Aucun chunk, eclairage,
        // fluide ou mesh n'est cree pendant une migration de sauvegarde.
        set_chunk_override_cell(
            entry,
            block_index,
            generated_block,
            generated_water_state,
            generated_block,
            generated_water_state,
            player_placed_mask_test(
                entry.player_placed_mask,
                block_index),
            nullptr);
        if (entry.generator_mismatch_count == 0U &&
            entry.player_placed_count == 0U) {
            chunk_overrides_.erase(override_iterator);
        }
        return true;
    }

    const auto generated_block = generator_.sample_block(x, y, z);
    const auto generated_water_state = generator_.sample_water_state(x, y, z);
    if (iterator == chunks_.end()) {
        throw std::runtime_error("Chunk disappeared during restore_generated_cell");
    }

    auto& record = iterator->second;
    auto& chunk = record.chunk;
    const auto previous_block = chunk.get_local(local.x, local.y, local.z);
    const auto previous_water_state = chunk.get_water_state_local(local.x, local.y, local.z);
    if (previous_block == generated_block && previous_water_state == generated_water_state) {
        return false;
    }

    if (previous_block != generated_block) {
        chunk.set_local(local.x, local.y, local.z, generated_block);
    }
    if (previous_water_state != generated_water_state) {
        chunk.set_water_state_local(local.x, local.y, local.z, generated_water_state);
    }

    update_chunk_override_after_cell_change(
        chunk_coord,
        local,
        previous_block,
        generated_block,
        previous_water_state,
        generated_water_state);

    if (previous_block != generated_block) {
        update_chunk_emissive_cache(record, local, previous_block, generated_block);
        mark_sky_column_dirty(chunk_coord, local.x, local.z);
        mark_chunk_and_neighbors_lighting_dirty(chunk_coord);
        remove_unsupported_torches_around(x, y, z);
    }

    mark_chunk_and_neighbors_dirty(chunk_coord, local);
    enqueue_fluid_cell({x, y, z});
    enqueue_adjacent_fluid_cells({x, y, z});
    return true;
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
    return raycast(origin, direction, max_distance, WorldRaycastMode::Selection);
}

auto World::raycast_visibility(const glm::vec3& origin,
                               const glm::vec3& direction,
                               float max_distance) const -> RaycastHit {
    return raycast(origin, direction, max_distance, WorldRaycastMode::VisibilityOpaque);
}

auto World::raycast_collidable(const glm::vec3& origin,
                               const glm::vec3& direction,
                               float max_distance) const -> RaycastHit {
    return raycast(origin, direction, max_distance, WorldRaycastMode::ProjectileCollidable);
}

auto World::raycast_water_or_opaque(
    const glm::vec3& origin,
    const glm::vec3& direction,
    float max_distance) const -> RaycastHit {
    return raycast(
        origin,
        direction,
        max_distance,
        WorldRaycastMode::WaterOrOpaque);
}

auto World::raycast(const glm::vec3& origin,
                    const glm::vec3& direction,
                    float max_distance,
                    WorldRaycastMode mode) const -> RaycastHit {
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

    const auto blocks_ray = [mode](BlockId block_id) noexcept -> bool {
        switch (mode) {
        case WorldRaycastMode::VisibilityOpaque:
        case WorldRaycastMode::WaterOrOpaque:
            return is_block_opaque(block_id);
        case WorldRaycastMode::ProjectileCollidable:
            return is_block_collidable(block_id);
        case WorldRaycastMode::Selection:
        default:
            return is_block_targetable(block_id);
        }
    };

    const auto water_or_opaque_hit = [&](const BlockCoord& cell,
                                         const BlockCoord& adjacent,
                                         float distance)
        -> std::optional<RaycastHit> {
        if (mode != WorldRaycastMode::WaterOrOpaque) {
            return std::nullopt;
        }
        const auto block_id = get_block(cell.x, cell.y, cell.z);
        if (is_block_opaque(block_id)) {
            return RaycastHit {
                true,
                cell,
                adjacent,
                block_id,
                distance,
            };
        }
        if (has_water(cell.x, cell.y, cell.z)) {
            return RaycastHit {
                true,
                cell,
                adjacent,
                to_block_id(BlockType::Water),
                distance,
            };
        }
        return std::nullopt;
    };

    if (const auto hit = water_or_opaque_hit(current, current, 0.0F);
        hit.has_value()) {
        return *hit;
    }

    const auto starting_block = get_block(current.x, current.y, current.z);
    if (blocks_ray(starting_block)) {
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

        if (const auto hit =
                water_or_opaque_hit(current, previous, travelled);
            hit.has_value()) {
            return *hit;
        }

        const auto block_id = get_block(current.x, current.y, current.z);
        if (blocks_ray(block_id)) {
            return {
                true,
                current,
                previous,
                block_id,
                travelled,
            };
        }

        // Je conserve le comportement historique de la selection : l'eau est
        // ciblable, mais elle ne masque ni la vision ni une balle.
        if (mode == WorldRaycastMode::Selection && has_water(current.x, current.y, current.z)) {
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
    return update_streaming(player_position, stream_radius_);
}

auto World::update_streaming(const glm::vec3& player_position, int requested_radius) -> WorldStreamingStats {
    WorldStreamingStats stats {};
    if (!is_finite_vec3(player_position)) {
        return stats;
    }

    const auto center = world_to_chunk(
        static_cast<int>(std::floor(player_position.x)),
        static_cast<int>(std::floor(player_position.z)));

    requested_radius = std::clamp(requested_radius, 0, stream_radius_);
    const auto center_changed = !has_stream_center_ || center != stream_center_;
    const auto radius_changed = !has_stream_center_ || requested_radius != active_stream_radius_;
    if (!center_changed && !radius_changed) {
        return stats;
    }

    stats.chunk_changed = center_changed;
    const auto previous_center = stream_center_;
    const auto had_previous_center = has_stream_center_;
    has_stream_center_ = true;
    stream_center_ = center;
    active_stream_radius_ = requested_radius;

    stats.unloaded_chunks = unload_far_chunks(center);

    prune_generation_queue(stats);
    if (!had_previous_center || radius_changed) {
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
    if (iterator == chunks_.end()) {
        return nullptr;
    }
    if (iterator->second.mesh_cache_dirty) {
        rebuild_chunk_mesh_cache(iterator->second);
    }
    return &iterator->second.mesh;
}

auto World::section_meshes_for(const ChunkCoord& coord) const
    -> const std::array<ChunkMeshData, kChunkSectionCount>* {
    const auto iterator = chunks_.find(coord);
    return iterator == chunks_.end() ? nullptr : &iterator->second.section_meshes;
}

auto World::organic_section_meshes_for(const ChunkCoord& coord) const
    -> const std::array<OrganicTerrainMesh, kChunkSectionCount>* {
    const auto iterator = chunks_.find(coord);
    return iterator == chunks_.end() ? nullptr : &iterator->second.organic_section_meshes;
}

auto World::architectural_section_meshes_for(const ChunkCoord& coord) const
    -> const std::array<ArchitecturalMesh, kChunkSectionCount>* {
    const auto iterator = chunks_.find(coord);
    return iterator == chunks_.end()
               ? nullptr
               : &iterator->second.architectural_section_meshes;
}

auto World::mesh_revision(const ChunkCoord& coord) const -> std::uint64_t {
    const auto iterator = chunks_.find(coord);
    return iterator == chunks_.end() ? 0 : iterator->second.mesh_revision;
}

auto World::visual_remesh_status(const ChunkCoord& coord) const noexcept
    -> VisualRemeshStatus {
    const auto iterator = chunks_.find(coord);
    if (iterator == chunks_.end()) {
        return {};
    }

    VisualRemeshStatus status {};
    status.published_revision = iterator->second.mesh_revision;
    if (iterator->second.modern_remesh == nullptr) {
        return status;
    }

    const auto& state = *iterator->second.modern_remesh;
    status.active = true;
    status.source_revision = state.source_revision;
    status.target_sections = state.target_sections;
    status.next_slice = state.next_slice;
    status.completed_slices = state.completed_slices;
    status.total_slices = state.total_slices;
    return status;
}

auto World::sample_visual_terrain(const TerrainVisualQuery& query) const
    -> std::optional<TerrainVisualSample> {

    if (visual_pipeline_ != VisualPipeline::ModernStylized ||
        !std::isfinite(query.world_position.x) ||
        !std::isfinite(query.world_position.y) ||
        !std::isfinite(query.world_position.z) ||
        !std::isfinite(query.maximum_distance) ||
        !std::isfinite(query.minimum_normal_y) ||
        query.maximum_distance < 0.0F) {
        return std::nullopt;
    }

    const auto query_x = static_cast<double>(query.world_position.x);
    const auto query_z = static_cast<double>(query.world_position.z);
    const auto maximum_distance =
        static_cast<double>(query.maximum_distance);
    const auto maximum_distance_squared =
        maximum_distance * maximum_distance;

    std::optional<TerrainVisualSample> closest {};
    ChunkCoord closest_chunk {};
    auto closest_section = std::size_t {0U};

    for (const auto& [coord, record] : chunks_) {
        // Je rejette d'abord les chunks hors du disque horizontal de la
        // requête afin de ne parcourir les triangles que localement.
        const auto minimum_x =
            static_cast<double>(coord.x) * static_cast<double>(kChunkSizeX);
        const auto maximum_x =
            minimum_x + static_cast<double>(kChunkSizeX);
        const auto minimum_z =
            static_cast<double>(coord.z) * static_cast<double>(kChunkSizeZ);
        const auto maximum_z =
            minimum_z + static_cast<double>(kChunkSizeZ);
        const auto delta_x =
            query_x < minimum_x
                ? minimum_x - query_x
                : (query_x > maximum_x ? query_x - maximum_x : 0.0);
        const auto delta_z =
            query_z < minimum_z
                ? minimum_z - query_z
                : (query_z > maximum_z ? query_z - maximum_z : 0.0);
        if (delta_x * delta_x + delta_z * delta_z >
            maximum_distance_squared) {
            continue;
        }

        for (std::size_t section_index = 0U;
             section_index < record.organic_section_meshes.size();
             ++section_index) {
            const auto section_minimum_y =
                static_cast<float>(section_min_y(section_index));
            const auto section_maximum_y =
                static_cast<float>(section_max_y(section_index) + 1);
            if (query.world_position.y + query.maximum_distance <
                    section_minimum_y ||
                query.world_position.y - query.maximum_distance >
                    section_maximum_y) {
                continue;
            }

            const auto sample = sample_terrain_visual_mesh(
                record.organic_section_meshes[section_index],
                query,
                record.mesh_revision);
            if (!sample.has_value()) {
                continue;
            }

            const auto strictly_closer =
                !closest.has_value() ||
                sample->distance_squared < closest->distance_squared;
            const auto same_distance =
                closest.has_value() &&
                sample->distance_squared == closest->distance_squared;
            const auto deterministic_tie_break =
                same_distance &&
                (coord.x < closest_chunk.x ||
                 (coord.x == closest_chunk.x &&
                  (coord.z < closest_chunk.z ||
                   (coord.z == closest_chunk.z &&
                    section_index < closest_section))));
            if (strictly_closer || deterministic_tie_break) {
                closest = sample;
                closest_chunk = coord;
                closest_section = section_index;
            }
        }
    }

    return closest;
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

auto World::pending_gpu_upload_count() const noexcept -> std::size_t {
    return pending_gpu_upload_set_.size();
}

auto World::seed() const noexcept -> int {
    return generator_.seed();
}

auto World::generation_profile() const noexcept -> WorldGenerationProfile {
    return generator_.profile();
}

auto World::generation_version() const noexcept -> WorldGenerationVersion {
    return generator_.generation_version();
}

auto World::backrooms_level() const noexcept -> int {
    return generator_.backrooms_level();
}

auto World::backrooms_level_at_y(float world_y) const noexcept -> int {
    return generator_.backrooms_level_at_y(world_y);
}

auto World::backrooms_theme_at_y(float world_y) const noexcept
    -> BackroomsTheme {
    return generator_.backrooms_theme_at_y(world_y);
}

auto World::backrooms_spawn_block(int logical_level) const noexcept
    -> BlockCoord {
    return generator_.backrooms_spawn_block(logical_level);
}

auto World::visual_pipeline() const noexcept -> VisualPipeline {
    return visual_pipeline_;
}

void World::set_visual_pipeline(VisualPipeline visual_pipeline) {
    if (visual_pipeline_ == visual_pipeline) {
        return;
    }

    visual_pipeline_ = visual_pipeline;
    for (auto& [coord, record] : chunks_) {
        record.modern_remesh.reset();
        record.chunk.mark_dirty();
        if (visual_pipeline_ == VisualPipeline::LegacyVoxel) {
            record.organic_section_meshes = {};
            record.organic_vertex_capacity_hints = {};
            record.organic_index_capacity_hints = {};
            record.architectural_section_meshes = {};
            record.architectural_vertex_capacity_hints = {};
            record.architectural_index_capacity_hints = {};
        }
        enqueue_mesh_rebuild(coord, true);
    }
}

auto World::stream_radius() const noexcept -> int {
    return stream_radius_;
}

auto World::sample_generated_surface(int world_x, int world_z) const noexcept
    -> TerrainSurfaceSample {
    return generator_.sample_surface(world_x, world_z);
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
    return pending_generation_queue_.size() + (active_generation_job_ != nullptr ? 1U : 0U);
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

auto World::memory_stats() const noexcept -> WorldMemoryStats {
    WorldMemoryStats stats {};
    stats.loaded_chunks = chunks_.size();
    stats.override_chunks = chunk_overrides_.size();

    const auto account_mesh = [&](const ChunkMeshData& mesh) {
        stats.mesh_vertex_capacity += mesh.vertices.capacity() + mesh.water_vertices.capacity();
        stats.mesh_index_capacity += mesh.indices.capacity() + mesh.water_indices.capacity();
        stats.mesh_cpu_bytes +=
            mesh.vertices.capacity() * sizeof(ChunkVertex) +
            mesh.water_vertices.capacity() * sizeof(WaterVertex) +
            (mesh.indices.capacity() + mesh.water_indices.capacity()) * sizeof(std::uint32_t);
    };
    const auto account_organic_mesh = [&](const OrganicTerrainMesh& mesh) {
        stats.mesh_vertex_capacity += mesh.vertices.capacity();
        stats.mesh_index_capacity += mesh.indices.capacity();
        stats.mesh_cpu_bytes +=
            mesh.vertices.capacity() * sizeof(TerrainVertex) +
            mesh.indices.capacity() * sizeof(std::uint32_t);
    };
    const auto account_architectural_mesh = [&](const ArchitecturalMesh& mesh) {
        stats.mesh_vertex_capacity += mesh.vertices.capacity();
        stats.mesh_index_capacity += mesh.indices.capacity();
        stats.mesh_cpu_bytes +=
            mesh.vertices.capacity() * sizeof(HardSurfaceVertex) +
            mesh.indices.capacity() * sizeof(std::uint32_t) +
            mesh.quads.capacity() * sizeof(ArchitecturalQuad) +
            mesh.fixtures.capacity() *
                sizeof(ArchitecturalFixtureInstance);
    };

    for (const auto& [coord, record] : chunks_) {
        (void)coord;
        // Je compte le payload du noeud de map et les capacites reellement
        // reservees, pas seulement le nombre d'elements actuellement utilises.
        stats.chunk_cpu_bytes += sizeof(ChunkCoord) + sizeof(ChunkRecord) + 2U * sizeof(void*);
        stats.chunk_cpu_bytes += record.emissive_blocks.capacity() * sizeof(BlockCoord);
        account_mesh(record.mesh);
        for (const auto& section_mesh : record.section_meshes) {
            account_mesh(section_mesh);
        }
        for (const auto& organic_mesh : record.organic_section_meshes) {
            account_organic_mesh(organic_mesh);
        }
        for (const auto& architectural_mesh : record.architectural_section_meshes) {
            account_architectural_mesh(architectural_mesh);
        }
        if (record.modern_remesh != nullptr) {
            stats.chunk_cpu_bytes += sizeof(ModernVisualRemeshState);
            for (const auto& staged_mesh :
                 record.modern_remesh->staged_section_meshes) {
                account_mesh(staged_mesh);
            }
            for (const auto& staged_mesh :
                 record.modern_remesh->staged_organic_meshes) {
                account_organic_mesh(staged_mesh);
            }
            for (const auto& staged_mesh :
                 record.modern_remesh->staged_architectural_meshes) {
                account_architectural_mesh(staged_mesh);
            }
        }
    }

    stats.override_bytes = chunk_overrides_.size() *
                           (sizeof(ChunkCoord) + sizeof(ChunkOverrideEntry) + 2U * sizeof(void*));
    for (const auto& [coord, entry] : chunk_overrides_) {
        (void)coord;
        stats.override_bytes += entry.sparse_cells.capacity() * sizeof(ChunkOverrideCell);
        if (entry.dense != nullptr) {
            stats.override_bytes += sizeof(DenseChunkOverride);
        }
    }

    if (active_lighting_job_.has_value()) {
        for (const auto& buffer : active_lighting_job_->block_light_buffers) {
            stats.lighting_cpu_bytes += buffer.capacity() * sizeof(std::uint8_t);
        }
        stats.lighting_cpu_bytes += active_lighting_job_->queue.size() * sizeof(LightNode);
    }

    if (active_generation_job_ != nullptr) {
        stats.generation_cpu_bytes = sizeof(WorldGenerator::ChunkGenerationState);
    }

    stats.fluid_cpu_bytes =
        (fluid_pressure_frontier_.capacity() + fluid_pressure_visited_.capacity()) * sizeof(BlockCoord) +
        fluid_pressure_head_cache_.size() * sizeof(std::pair<const BlockCoord, WaterPressureHead>) +
        (fluid_pressure_head_missing_cache_.size() + fluid_pressure_seen_.size()) * sizeof(BlockCoord) +
        (fluid_pressure_head_cache_.bucket_count() + fluid_pressure_head_missing_cache_.bucket_count() +
         fluid_pressure_seen_.bucket_count()) *
            sizeof(void*);

    stats.queue_cpu_bytes += pending_generation_queue_.size() * sizeof(ChunkCoord);
    stats.queue_cpu_bytes += pending_fluid_queue_.size() * sizeof(BlockCoord);
    stats.queue_cpu_bytes +=
        (pending_priority_mesh_queue_.size() + pending_mesh_queue_.size() +
         pending_gpu_uploads_.size() + pending_gpu_unloads_.size()) *
        sizeof(ChunkCoord);
    stats.queue_cpu_bytes += pending_lighting_queue_.size() * sizeof(PendingLightingUpdate);
    for (const auto& update : pending_lighting_queue_) {
        stats.queue_cpu_bytes += update.coverage.capacity() * sizeof(ChunkCoord);
    }

    const auto chunk_coord_set_bytes =
        (pending_generation_set_.size() + pending_mesh_set_.size() + pending_priority_mesh_set_.size() +
         deferred_mesh_invalidation_set_.size() + pending_lighting_set_.size() +
         pending_lighting_coverage_.size() + active_lighting_coverage_.size() +
         pending_gpu_upload_set_.size() + pending_gpu_unload_set_.size()) *
        (sizeof(ChunkCoord) + 2U * sizeof(void*));
    const auto block_coord_set_bytes =
        pending_fluid_set_.size() * (sizeof(BlockCoord) + 2U * sizeof(void*));
    stats.queue_cpu_bytes += chunk_coord_set_bytes + block_coord_set_bytes;

    stats.world_cpu_bytes = sizeof(World) + stats.chunk_cpu_bytes + stats.mesh_cpu_bytes +
                            stats.override_bytes + stats.fluid_cpu_bytes + stats.lighting_cpu_bytes +
                            stats.generation_cpu_bytes + stats.queue_cpu_bytes;
    return stats;
}

auto World::has_pending_work() const noexcept -> bool {
    if (!pending_generation_queue_.empty() ||
        active_generation_job_ != nullptr ||
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
            const auto iterator = chunks_.find(coord);
            if (iterator == chunks_.end()) {
                return false;
            }

            const auto& record = iterator->second;
            // Un remesh de couture conserve le mesh precedent affichable. Je
            // ne rebloque donc pas le spawn pendant le streaming exterieur.
            if (record.mesh_revision == 0) {
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
        if (override_entry.generator_mismatch_count == 0U &&
            override_entry.player_placed_count == 0U) {
            continue;
        }

        if (const auto loaded_iterator = chunks_.find(coord); loaded_iterator != chunks_.end()) {
            snapshots.push_back({
                coord,
                loaded_iterator->second.chunk.blocks(),
                loaded_iterator->second.chunk.water_state(),
                override_entry.player_placed_mask,
            });
            continue;
        }

        snapshots.push_back(materialize_chunk_override(coord, override_entry));
    }

    return snapshots;
}

auto World::capture_save_plan() const -> WorldSavePlan {
    WorldSavePlan plan {};
    plan.seed = generator_.seed();
    plan.generation_profile = generator_.profile();
    plan.generation_version = generator_.generation_version();
    plan.backrooms_level = generator_.backrooms_level();
    plan.chunks.reserve(chunk_overrides_.size());

    for (const auto& [coord, override_entry] : chunk_overrides_) {
        if (override_entry.generator_mismatch_count == 0U &&
            override_entry.player_placed_count == 0U) {
            continue;
        }

        WorldSavePlanChunk chunk_plan {};
        chunk_plan.coord = coord;
        chunk_plan.player_placed_mask =
            override_entry.player_placed_mask;
        if (override_entry.dense != nullptr) {
            chunk_plan.dense_blocks.assign(
                override_entry.dense->blocks.begin(),
                override_entry.dense->blocks.end());
            chunk_plan.dense_water_state.assign(
                override_entry.dense->water_state.begin(),
                override_entry.dense->water_state.end());
        } else {
            chunk_plan.sparse_cells.reserve(override_entry.sparse_cells.size());
            for (const auto& cell : override_entry.sparse_cells) {
                chunk_plan.sparse_cells.push_back({cell.index, cell.block, cell.water_state});
            }
        }
        plan.chunks.push_back(std::move(chunk_plan));
    }

    return plan;
}

void World::begin_restore_save_plan(WorldSavePlan plan) {
    if (plan.seed != generator_.seed() ||
        plan.generation_profile != generator_.profile() ||
        plan.generation_version != generator_.generation_version() ||
        plan.backrooms_level != generator_.backrooms_level()) {
        throw std::invalid_argument("World save plan does not match the destination world");
    }

    auto total_cells = std::size_t {0};
    auto seen_coords = std::unordered_set<ChunkCoord, ChunkCoordHash> {};
    seen_coords.reserve(plan.chunks.size());
    for (const auto& chunk : plan.chunks) {
        if (!seen_coords.insert(chunk.coord).second) {
            throw std::invalid_argument("World save plan contains duplicate chunks");
        }
        if (chunk.dense()) {
            if (!chunk.sparse_cells.empty() ||
                chunk.dense_blocks.size() != kChunkVolume ||
                chunk.dense_water_state.size() != kChunkVolume) {
                throw std::invalid_argument("World save plan contains an invalid dense chunk");
            }
            total_cells += kChunkVolume * 2U;
            continue;
        }
        const auto player_placed_count =
            player_placed_mask_count(
                chunk.player_placed_mask);
        if ((chunk.sparse_cells.empty() &&
             player_placed_count == 0U) ||
            !chunk.dense_water_state.empty()) {
            throw std::invalid_argument("World save plan contains an invalid sparse chunk");
        }
        // Le lecteur binaire valide deja chaque cellule au fil de l'I/O. Pour
        // un plan fourni directement, je reporte ce controle dans les tranches
        // afin que begin_restore_save_plan reste en O(nombre de chunks).
        total_cells +=
            chunk.sparse_cells.empty()
                ? 1U
                : chunk.sparse_cells.size();
    }

    // Je remplace atomiquement le plan logique; ses vecteurs seront liberes
    // chunk par chunk au fil de la restauration.
    chunk_overrides_.clear();
    if (plan.chunks.empty()) {
        save_restore_state_.reset();
        return;
    }

    SaveRestoreState state {};
    state.total_cells = total_cells;
    state.plan = std::move(plan);
    save_restore_state_ = std::move(state);
}

auto World::process_save_restore(std::size_t cell_budget, double max_ms) -> WorldSaveRestoreStats {
    using clock = std::chrono::steady_clock;
    WorldSaveRestoreStats stats {};
    if (!save_restore_state_.has_value()) {
        return stats;
    }

    auto& state = *save_restore_state_;
    const auto time_limited = has_time_budget(max_ms);
    const auto deadline = time_limited
                              ? clock::now() + std::chrono::duration<double, std::milli>(std::max(0.0, max_ms))
                              : (clock::time_point::max)();
    auto remaining = cell_budget;
    const auto time_expired = [&] {
        return time_limited && clock::now() >= deadline;
    };
    const auto complete_current_chunk = [&](WorldSavePlanChunk& chunk_plan) {
        if (state.pending_override.generator_mismatch_count > 0U ||
            state.pending_override.player_placed_count > 0U) {
            auto [override_iterator, inserted] = chunk_overrides_.insert_or_assign(
                chunk_plan.coord,
                std::move(state.pending_override));
            (void)inserted;
            if (auto loaded = chunks_.find(chunk_plan.coord); loaded != chunks_.end()) {
                apply_chunk_override_to_record(loaded->second, override_iterator->second);
                enqueue_lighting_update(chunk_plan.coord);
                invalidate_loaded_mesh_neighbors_for_chunk_load(chunk_plan.coord);
                apply_chunk_load_fluid_revalidation(chunk_plan.coord);
            }
        }

        // Je rends immediatement au systeme les buffers lus pour ce chunk.
        chunk_plan = {};
        ++state.next_chunk;
        state.next_cell = 0U;
        state.generated_chunk.reset();
        state.pending_override = {};
        ++stats.completed_chunks;
    };

    while (state.next_chunk < state.plan.chunks.size() && remaining > 0U && !time_expired()) {
        auto& chunk_plan = state.plan.chunks[state.next_chunk];
        if (!chunk_plan.dense()) {
            if (state.next_cell == 0U &&
                state.pending_override.sparse_cells.capacity() == 0U &&
                state.pending_override.player_placed_count == 0U) {
                state.pending_override.sparse_cells.reserve(
                    std::min(chunk_plan.sparse_cells.size(), kSparseOverrideCellLimit));
                state.pending_override.player_placed_mask =
                    chunk_plan.player_placed_mask;
                state.pending_override.player_placed_count =
                    player_placed_mask_count(
                        chunk_plan.player_placed_mask);
                if (chunk_plan.sparse_cells.empty() &&
                    state.pending_override.player_placed_count > 0U) {
                    --remaining;
                    ++stats.processed_cells;
                    ++state.processed_cells;
                }
            }
            while (state.next_cell < chunk_plan.sparse_cells.size() && remaining > 0U && !time_expired()) {
                const auto plan_cell_index = state.next_cell;
                const auto& saved_cell = chunk_plan.sparse_cells[plan_cell_index];
                const auto block_index = static_cast<std::size_t>(saved_cell.index);
                if (block_index >= kChunkVolume ||
                    (plan_cell_index > 0U &&
                     saved_cell.index <= chunk_plan.sparse_cells[plan_cell_index - 1U].index)) {
                    throw std::invalid_argument("World save plan sparse cells are not strictly ordered");
                }
                ++state.next_cell;
                const auto local_x = static_cast<int>(block_index % static_cast<std::size_t>(kChunkSizeX));
                const auto yz_index = block_index / static_cast<std::size_t>(kChunkSizeX);
                const auto local_z = static_cast<int>(yz_index % static_cast<std::size_t>(kChunkSizeZ));
                const auto local_y = static_cast<int>(yz_index / static_cast<std::size_t>(kChunkSizeZ));
                const auto world_x = chunk_plan.coord.x * kChunkSizeX + local_x;
                const auto world_z = chunk_plan.coord.z * kChunkSizeZ + local_z;
                const auto generated_block = generator_.sample_block(world_x, local_y, world_z);
                const auto generated_water = generator_.sample_water_state(world_x, local_y, world_z);
                auto saved_water = saved_cell.water_state;
                if (water_level_from_state(saved_water) == kMaxWaterLevel &&
                    water_state_is_source(saved_water) &&
                    water_state_is_infinite(generated_water)) {
                    saved_water = make_water_state(kMaxWaterLevel, true, true);
                }

                if (saved_cell.block != generated_block || saved_water != generated_water) {
                    state.pending_override.changed_cells.set(block_index);
                    ++state.pending_override.generator_mismatch_count;
                    state.pending_override.sparse_cells.push_back({
                        saved_cell.index,
                        saved_cell.block,
                        saved_water,
                        generated_block,
                        generated_water,
                    });
                }
                --remaining;
                ++stats.processed_cells;
                ++state.processed_cells;
            }

            if (state.next_cell == chunk_plan.sparse_cells.size()) {
                complete_current_chunk(chunk_plan);
            }
            continue;
        }

        if (state.generated_chunk == nullptr) {
            state.generated_chunk = std::make_unique<WorldGenerator::ChunkGenerationState>(
                generator_.begin_chunk_generation(chunk_plan.coord));
        }

        if (!generator_.is_chunk_generation_complete(*state.generated_chunk)) {
            // Le generateur avance par colonne de 128 cellules. Je n'entame
            // jamais une colonne si le budget ne peut pas la couvrir.
            if (remaining < static_cast<std::size_t>(kChunkHeight)) {
                break;
            }
            generator_.advance_chunk_generation(*state.generated_chunk, 1U);
            remaining -= static_cast<std::size_t>(kChunkHeight);
            stats.processed_cells += static_cast<std::size_t>(kChunkHeight);
            state.processed_cells += static_cast<std::size_t>(kChunkHeight);
            if (!generator_.is_chunk_generation_complete(*state.generated_chunk)) {
                continue;
            }

            state.pending_override = {};
            state.pending_override.dense = std::make_unique<DenseChunkOverride>();
            std::copy(chunk_plan.dense_blocks.begin(), chunk_plan.dense_blocks.end(),
                      state.pending_override.dense->blocks.begin());
            std::copy(chunk_plan.dense_water_state.begin(), chunk_plan.dense_water_state.end(),
                      state.pending_override.dense->water_state.begin());
            state.pending_override.dense->generated_blocks = state.generated_chunk->chunk.blocks();
            state.pending_override.dense->generated_water_state = state.generated_chunk->chunk.water_state();
            state.pending_override.player_placed_mask =
                chunk_plan.player_placed_mask;
            state.pending_override.player_placed_count =
                player_placed_mask_count(
                    chunk_plan.player_placed_mask);
        }

        const auto comparison_count = kChunkVolume;
        const auto& generated_blocks = state.generated_chunk->chunk.blocks();
        const auto& generated_water = state.generated_chunk->chunk.water_state();
        while (state.next_cell < comparison_count && remaining > 0U && !time_expired()) {
            const auto block_index = state.next_cell++;
            const auto saved_block = chunk_plan.dense_blocks[block_index];
            auto saved_water = chunk_plan.dense_water_state[block_index];
            if (water_level_from_state(saved_water) == kMaxWaterLevel &&
                water_state_is_source(saved_water) &&
                water_state_is_infinite(generated_water[block_index])) {
                saved_water = make_water_state(kMaxWaterLevel, true, true);
            }

            if (state.pending_override.dense != nullptr) {
                state.pending_override.dense->water_state[block_index] = saved_water;
            }
            if (saved_block != generated_blocks[block_index] || saved_water != generated_water[block_index]) {
                state.pending_override.changed_cells.set(block_index);
                ++state.pending_override.generator_mismatch_count;
            }

            --remaining;
            ++stats.processed_cells;
            ++state.processed_cells;
        }

        if (state.next_cell < comparison_count) {
            continue;
        }

        complete_current_chunk(chunk_plan);
    }

    const auto processed_total = state.processed_cells;
    const auto total = state.total_cells;
    if (state.next_chunk == state.plan.chunks.size()) {
        save_restore_state_.reset();
        stats.pending_cells = 0U;
        stats.progress = 1.0F;
        return stats;
    }

    stats.pending_cells = total > processed_total ? total - processed_total : 0U;
    stats.progress = total == 0U
                         ? 1.0F
                         : std::clamp(static_cast<float>(processed_total) / static_cast<float>(total), 0.0F, 1.0F);
    return stats;
}

auto World::has_pending_save_restore() const noexcept -> bool {
    return save_restore_state_.has_value();
}

auto World::save_restore_progress() const noexcept -> float {
    if (!save_restore_state_.has_value()) {
        return 1.0F;
    }
    const auto& state = *save_restore_state_;
    if (state.total_cells == 0U) {
        return 1.0F;
    }
    return std::clamp(
        static_cast<float>(state.processed_cells) / static_cast<float>(state.total_cells),
        0.0F,
        1.0F);
}

void World::replace_chunk_snapshots(const std::vector<WorldChunkSnapshot>& snapshots) {
    chunk_overrides_.clear();
    for (const auto& snapshot : snapshots) {
        auto entry =
            make_chunk_override_entry(
                snapshot.coord,
                snapshot.blocks,
                snapshot.water_state,
                snapshot.player_placed_mask);
        if (!entry.has_value()) {
            continue;
        }
        chunk_overrides_.insert_or_assign(snapshot.coord, std::move(*entry));
    }

    for (auto& [coord, record] : chunks_) {
        const auto iterator = chunk_overrides_.find(coord);
        if (iterator == chunk_overrides_.end()) {
            continue;
        }
        apply_chunk_override_to_record(record, iterator->second);
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

    if (active_generation_job_ != nullptr && active_generation_job_->chunk.coord() == coord) {
        generator_.advance_chunk_generation(
            *active_generation_job_,
            static_cast<std::size_t>(kChunkSizeX * kChunkSizeZ));
        auto generated_chunk = std::move(active_generation_job_->chunk);
        active_generation_job_.reset();
        install_generated_chunk(std::move(generated_chunk));
        return;
    }

    Chunk generated_chunk {coord};
    generator_.generate_chunk(generated_chunk);
    install_generated_chunk(std::move(generated_chunk));
}

void World::install_generated_chunk(Chunk&& chunk) {
    const auto coord = chunk.coord();
    auto [iterator, inserted] = chunks_.try_emplace(coord, std::move(chunk));
    if (!inserted) {
        return;
    }

    iterator->second.chunk.clear_lighting();
    if (const auto override_iterator = chunk_overrides_.find(coord); override_iterator != chunk_overrides_.end()) {
        apply_chunk_override_to_record(iterator->second, override_iterator->second);
    } else if (
        generator_.profile() ==
        WorldGenerationProfile::Backrooms) {
        // Les plafonniers BackRooms sont générés directement dans le chunk.
        // Le cache doit donc être construit avant le premier calcul de lumière.
        refresh_chunk_emissive_cache(iterator->second);
    } else {
        // Les profils extérieurs ne placent pas de bloc émissif pendant la
        // génération et évitent ainsi un scan complet de chaque chunk.
        iterator->second.emissive_blocks.clear();
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
    for (int radius = 1; radius <= active_stream_radius_; ++radius) {
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
        const auto x = next_center.x + (dx > 0 ? active_stream_radius_ : -active_stream_radius_);
        for (int z = next_center.z - active_stream_radius_; z <= next_center.z + active_stream_radius_; ++z) {
            enqueue_generation_candidate({x, z}, &stats);
        }
    }

    if (dz != 0) {
        const auto z = next_center.z + (dz > 0 ? active_stream_radius_ : -active_stream_radius_);
        for (int x = next_center.x - active_stream_radius_; x <= next_center.x + active_stream_radius_; ++x) {
            enqueue_generation_candidate({x, z}, &stats);
        }
    }
}

void World::prune_generation_queue(WorldStreamingStats& stats) {
    if (active_generation_job_ != nullptr &&
        !is_inside_active_stream(active_generation_job_->chunk.coord())) {
        active_generation_job_.reset();
        ++stats.generation_pruned;
    }

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
    if (uses_static_poolrooms_water()) {
        // Dans mes Poolrooms, la nappe fait partie de l'architecture procédurale.
        // Je ne la confie pas au simulateur océanique, sinon elle se viderait et
        // recoloniserait les zones sèches au fil des chargements de chunks.
        return;
    }
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

void World::enqueue_chunk_fluid_boundary_updates(const ChunkCoord& coord,
                                                 const ChunkCoord& boundary_direction) {
    const auto* chunk = find_chunk(coord);
    if (chunk == nullptr ||
        (std::abs(boundary_direction.x) + std::abs(boundary_direction.z)) != 1) {
        return;
    }

    const auto inspect_water_cell = [&](int local_x, int y, int local_z) {
        if (!chunk->has_water_local(local_x, y, local_z)) {
            return;
        }

        const BlockCoord world_coord {
            coord.x * kChunkSizeX + local_x,
            y,
            coord.z * kChunkSizeZ + local_z,
        };
        if (can_water_flow_into_loaded(world_coord.x, world_coord.y - 1, world_coord.z) ||
            can_water_flow_into_loaded(world_coord.x + 1, world_coord.y, world_coord.z) ||
            can_water_flow_into_loaded(world_coord.x - 1, world_coord.y, world_coord.z) ||
            can_water_flow_into_loaded(world_coord.x, world_coord.y, world_coord.z + 1) ||
            can_water_flow_into_loaded(world_coord.x, world_coord.y, world_coord.z - 1)) {
            enqueue_fluid_cell(world_coord);
        }
    };

    if (boundary_direction.x != 0) {
        const auto local_x = boundary_direction.x < 0 ? 0 : kChunkSizeX - 1;
        for (int y = kWorldMinY; y <= kWorldMaxY; ++y) {
            for (int local_z = 0; local_z < kChunkSizeZ; ++local_z) {
                inspect_water_cell(local_x, y, local_z);
            }
        }
        return;
    }

    const auto local_z = boundary_direction.z < 0 ? 0 : kChunkSizeZ - 1;
    for (int y = kWorldMinY; y <= kWorldMaxY; ++y) {
        for (int local_x = 0; local_x < kChunkSizeX; ++local_x) {
            inspect_water_cell(local_x, y, local_z);
        }
    }
}

void World::apply_chunk_load_fluid_revalidation(const ChunkCoord& coord) {
    // Un snapshot peut modifier de l'eau au milieu du chunk; lui seul exige le
    // scan complet. Pour un chunk procedural, seules les quatre coutures qui
    // viennent de devenir chargees peuvent debloquer un ecoulement.
    if (chunk_overrides_.contains(coord)) {
        enqueue_chunk_fluid_updates(coord);
    }
    for (const auto& offset : kNeighborOffsets) {
        const ChunkCoord neighbor_coord {coord.x + offset.x, coord.z + offset.z};
        if (find_chunk(neighbor_coord) == nullptr) {
            continue;
        }
        enqueue_chunk_fluid_boundary_updates(coord, offset);
        enqueue_chunk_fluid_boundary_updates(neighbor_coord, {-offset.x, -offset.z});
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
            // Je remonte les chunks proches du centre devant le backlog lointain
            // pour qu'un deplacement ne revele jamais une colonne non maillee.
            enqueue_mesh_rebuild(coord, should_prioritize_mesh_invalidation(coord));
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
    while (remaining > 0 && (active_generation_job_ != nullptr || !pending_generation_queue_.empty())) {
        if (time_limited && clock::now() >= deadline) {
            break;
        }

        if (active_generation_job_ == nullptr) {
            const auto coord = pending_generation_queue_.front();
            pending_generation_queue_.pop_front();
            pending_generation_set_.erase(coord);

            if (chunks_.contains(coord) || !is_inside_active_stream(coord)) {
                continue;
            }

            active_generation_job_ =
                std::make_unique<WorldGenerator::ChunkGenerationState>(generator_.begin_chunk_generation(coord));
        }

        // Une colonne est l'unite preemptible: le budget temporel n'est plus
        // bloque par la generation atomique d'un chunk complet.
        generator_.advance_chunk_generation(*active_generation_job_, 1U);
        if (!generator_.is_chunk_generation_complete(*active_generation_job_)) {
            continue;
        }

        auto generated_chunk = std::move(active_generation_job_->chunk);
        active_generation_job_.reset();
        install_generated_chunk(std::move(generated_chunk));
        ++stats.generated_chunks;
        --remaining;
    }
}

void World::process_fluid_queue(std::size_t budget, double max_ms, WorldWorkStats& stats) {
    using clock = std::chrono::steady_clock;

    if (uses_static_poolrooms_water()) {
        // Je purge aussi une éventuelle file héritée d'une restauration afin que
        // le niveau soit totalement stable avant sa première image jouable.
        pending_fluid_queue_.clear();
        pending_fluid_set_.clear();
        return;
    }

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
    fluid_pressure_head_cache_.clear();
    fluid_pressure_head_missing_cache_.clear();
    auto& pressure_head_cache = fluid_pressure_head_cache_;
    auto& pressure_head_missing_cache = fluid_pressure_head_missing_cache_;

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
            stats.fluid_cells_changed += changed_cell_count;
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

    if (budget == 0U) {
        return;
    }

    auto remaining = budget;
    const auto time_limited = has_time_budget(max_ms);
    if (time_limited && max_ms <= 0.0) {
        return;
    }

    const auto deadline = clock::now() + std::chrono::duration<double, std::milli>(std::max(0.0, max_ms));
    std::size_t processed_since_deadline_check = 0;
    const auto backrooms_lighting =
        generation_profile() == WorldGenerationProfile::Backrooms;
    while (true) {
        if (remaining == 0U) {
            break;
        }
        if (time_limited && clock::now() >= deadline) {
            break;
        }

        if (!active_lighting_job_.has_value()) {
            while (!pending_lighting_queue_.empty() && remaining > 0U &&
                   (!time_limited || clock::now() < deadline)) {
                const auto pending_update = std::move(pending_lighting_queue_.front());
                pending_lighting_queue_.pop_front();
                pending_lighting_set_.erase(pending_update.anchor);
                for (const auto& covered_coord : pending_update.coverage) {
                    pending_lighting_coverage_.erase(covered_coord);
                }

                LightingJob job {};
                job.anchor = pending_update.anchor;
                const auto setup_start = clock::now();
                const auto initialized = initialize_lighting_job(job);
                stats.lighting_setup_ms +=
                    std::chrono::duration<double, std::milli>(clock::now() - setup_start).count();
                ++stats.lighting_work_units_processed;
                --remaining;
                if (!initialized) {
                    continue;
                }
                ++stats.lighting_jobs_started;

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
            if (remaining == 0U || (time_limited && clock::now() >= deadline)) {
                break;
            }
        }

        auto& job = *active_lighting_job_;
        if (job.queue.empty()) {
            active_lighting_coverage_.clear();
            const auto finalize_start = clock::now();
            finalize_lighting_job(job);
            stats.lighting_finalize_ms +=
                std::chrono::duration<double, std::milli>(clock::now() - finalize_start).count();
            active_lighting_job_.reset();
            ++stats.lighting_work_units_processed;
            --remaining;
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
        ++stats.lighting_work_units_processed;
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

            auto attenuation = std::uint8_t {1U};
            const auto vertical_light_stride =
                backrooms_lighting &&
                        backrooms_theme_at_y(
                            static_cast<float>(neighbor.y)) ==
                            BackroomsTheme::Poolrooms
                    ? kPoolroomsVerticalLightStride
                    : kBackroomsVerticalLightStride;
            if (backrooms_lighting &&
                offset.y != 0 &&
                neighbor.y % vertical_light_stride != 0) {
                // Les dalles fluorescentes sont des sources surfaciques : leur
                // énergie descend plus loin qu'elle ne s'étale à l'horizontale.
                // Je conserve un champ entier déterministe. Dans les Poolrooms,
                // quatre cellules verticales ne coûtent qu'un niveau : les
                // plafonds très hauts éclairent ainsi encore leur bassin, sans
                // créer le moindre photon lorsqu'une zone n'a aucune lampe.
                attenuation = 0U;
            }
            const auto propagated = static_cast<std::uint8_t>(
                node.light_level - attenuation);
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
    const auto first_publish_ready = [&](const ChunkCoord& coord) {
        if (visual_pipeline_ != VisualPipeline::ModernStylized ||
            !pending_mesh_set_.contains(coord) ||
            chunk_has_pending_lighting(coord)) {
            return false;
        }

        const auto iterator = chunks_.find(coord);
        return iterator != chunks_.end() &&
               iterator->second.mesh_revision == 0U &&
               iterator->second.chunk.is_dirty();
    };
    const auto absolute_chunk_delta = [](int left, int right) noexcept {
        const auto delta =
            static_cast<std::int64_t>(left) -
            static_cast<std::int64_t>(right);
        return static_cast<std::uint64_t>(
            delta < 0 ? -delta : delta);
    };
    const auto first_publish_precedes =
        [&](const ChunkCoord& left, const ChunkCoord& right) {
            const auto left_dx =
                has_stream_center_
                    ? absolute_chunk_delta(left.x, stream_center_.x)
                    : 0U;
            const auto left_dz =
                has_stream_center_
                    ? absolute_chunk_delta(left.z, stream_center_.z)
                    : 0U;
            const auto right_dx =
                has_stream_center_
                    ? absolute_chunk_delta(right.x, stream_center_.x)
                    : 0U;
            const auto right_dz =
                has_stream_center_
                    ? absolute_chunk_delta(right.z, stream_center_.z)
                    : 0U;
            const auto left_ring = std::max(left_dx, left_dz);
            const auto right_ring = std::max(right_dx, right_dz);
            if (left_ring != right_ring) {
                return left_ring < right_ring;
            }

            const auto left_manhattan = left_dx + left_dz;
            const auto right_manhattan = right_dx + right_dz;
            if (left_manhattan != right_manhattan) {
                return left_manhattan < right_manhattan;
            }
            return left.x < right.x ||
                   (left.x == right.x && left.z < right.z);
        };
    const auto select_first_publish = [&]() -> std::optional<ChunkCoord> {
        auto selected = std::optional<ChunkCoord> {};
        for (const auto& coord : pending_mesh_set_) {
            if (!first_publish_ready(coord)) {
                continue;
            }
            if (!selected.has_value() ||
                first_publish_precedes(coord, *selected)) {
                selected = coord;
            }
        }
        return selected;
    };
    const auto remove_queued_mesh_coord = [&](const ChunkCoord& coord) {
        pending_priority_mesh_queue_.erase(
            std::remove(
                pending_priority_mesh_queue_.begin(),
                pending_priority_mesh_queue_.end(),
                coord),
            pending_priority_mesh_queue_.end());
        pending_mesh_queue_.erase(
            std::remove(
                pending_mesh_queue_.begin(),
                pending_mesh_queue_.end(),
                coord),
            pending_mesh_queue_.end());
        pending_priority_mesh_set_.erase(coord);
        pending_mesh_set_.erase(coord);
    };

    auto remaining = budget;
    while (remaining > 0 &&
           (!pending_priority_mesh_queue_.empty() || !pending_mesh_queue_.empty())) {
        if (time_limited && clock::now() >= deadline) {
            break;
        }

        if (first_publish_mesh_in_progress_.has_value() &&
            !first_publish_ready(*first_publish_mesh_in_progress_)) {
            first_publish_mesh_in_progress_.reset();
        }
        if (!first_publish_mesh_in_progress_.has_value()) {
            first_publish_mesh_in_progress_ =
                select_first_publish();
        }

        const auto first_publish =
            first_publish_mesh_in_progress_.has_value();
        const auto prioritize =
            first_publish ||
            !pending_priority_mesh_queue_.empty();
        const auto coord =
            first_publish
                ? *first_publish_mesh_in_progress_
                : (prioritize
                       ? pending_priority_mesh_queue_.front()
                       : pending_mesh_queue_.front());
        if (first_publish) {
            // Je retire l'entrée de sa file historique, puis je la réinsère en
            // tête logique tant que sa première révision n'est pas publiée.
            remove_queued_mesh_coord(coord);
        } else if (prioritize) {
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

        const auto published = rebuild_chunk_mesh(iterator->second);
        ++stats.mesh_sections_processed;
        --remaining;
        if (!published) {
            enqueue_mesh_rebuild(coord, prioritize);
            continue;
        }

        if (first_publish) {
            first_publish_mesh_in_progress_.reset();
        }
        ++stats.meshed_chunks;
        if (prioritize) {
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
        // Le job capture ici l'etat sale courant. Une modification ulterieure
        // remettra ce drapeau et provoquera proprement un job de suivi.
        iterator->second.chunk.clear_lighting_dirty();
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

        if (record.chunk.is_dirty()) {
            enqueue_mesh_rebuild(coord);
        }
    }
}

auto World::unload_far_chunks(const ChunkCoord& center) -> std::size_t {
    std::vector<ChunkCoord> to_remove;
    to_remove.reserve(chunks_.size());

    const auto unload_radius = active_stream_radius_ + 1;
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

auto World::rebuild_chunk_mesh(ChunkRecord& record) -> bool {
    if (visual_pipeline_ == VisualPipeline::ModernStylized) {
        return rebuild_modern_chunk_mesh(record);
    }

    // Je conserve le chemin historique tel quel et j'abandonne seulement un
    // éventuel staging moderne devenu sans objet après un changement d'option.
    record.modern_remesh.reset();
    for (std::size_t section_index = 0; section_index < kChunkSectionCount; ++section_index) {
        if (!record.chunk.is_section_dirty(section_index)) {
            continue;
        }

        const auto modern_pipeline =
            visual_pipeline_ == VisualPipeline::ModernStylized;
        record.section_meshes[section_index] = mesher_.build_mesh_range(
            *this,
            record.chunk.coord(),
            section_min_y(section_index),
            section_max_y(section_index),
            record.section_mesh_vertex_capacity_hints[section_index],
            record.section_mesh_index_capacity_hints[section_index],
            modern_pipeline
                ? ChunkMeshContent::ModernNonOrganic
                : ChunkMeshContent::LegacyAll);
        record.section_mesh_vertex_capacity_hints[section_index] =
            std::max(record.section_meshes[section_index].total_vertex_count(), static_cast<std::size_t>(128));
        record.section_mesh_index_capacity_hints[section_index] =
            std::max(record.section_meshes[section_index].total_index_count(), static_cast<std::size_t>(192));

        if (modern_pipeline) {
            const auto coord = record.chunk.coord();
            const auto chunk_world_x = coord.x * kChunkSizeX;
            const auto chunk_world_z = coord.z * kChunkSizeZ;
            const OrganicTerrainSection section {
                {chunk_world_x, section_min_y(section_index), chunk_world_z},
                {
                    chunk_world_x + kChunkSizeX - 1,
                    section_max_y(section_index),
                    chunk_world_z + kChunkSizeZ - 1,
                },
            };
            auto& visual_mesh =
                record.organic_section_meshes[section_index];
            visual_mesh = {};
            if (chunk_section_has_organic_surface(
                    *this,
                    record.chunk,
                    section_index)) {
                visual_mesh = organic_mesher_.build_mesh(
                    section,
                    [this](int x, int y, int z) {
                        return OrganicTerrainCellSample {
                            peek_block_or_generated(x, y, z),
                            get_sky_light(x, y, z),
                            get_block_light(x, y, z),
                        };
                    },
                    record.organic_vertex_capacity_hints[section_index],
                    record.organic_index_capacity_hints[section_index]);
            }

            std::optional<VisualVegetationBuild> vegetation {};
            if (chunk_section_has_visual_vegetation(
                    record.chunk,
                    section_index)) {
                vegetation = build_visual_vegetation(
                    {
                        section.min,
                        section.max,
                        1,
                    },
                    [this](int x, int y, int z) {
                        return peek_block_or_generated(x, y, z);
                    },
                    static_cast<std::uint32_t>(seed()));
                const auto vegetation_mesh =
                    build_visual_vegetation_mesh(
                        *vegetation,
                        VisualVegetationLod::Medium,
                        StylizedPrimitiveLod::Low,
                        [this](int x, int y, int z) {
                            return VisualVegetationLighting {
                                get_sky_light(x, y, z),
                                get_block_light(x, y, z),
                            };
                        });
                append_organic_mesh(visual_mesh, vegetation_mesh);
            }

            auto& architectural_mesh =
                record.architectural_section_meshes[section_index];
            architectural_mesh = {};
            if (chunk_section_has_architecture(
                    record.chunk,
                    section_index)) {
                const ArchitecturalSection architectural_section {
                    section.min,
                    section.max,
                    1,
                };
                const ArchitecturalSampler architectural_sampler =
                    [this, &vegetation](int x, int y, int z) {
                    auto block = peek_block_or_generated(x, y, z);
                    if (vegetation.has_value() &&
                        coordinate_is_tree_wood(
                            *vegetation,
                            {x, y, z},
                            block)) {
                        block = to_block_id(BlockType::Air);
                    }
                    return ArchitecturalCellSample {
                        block,
                        get_sky_light(x, y, z),
                        get_block_light(x, y, z),
                    };
                };
                architectural_mesh = architectural_mesher_.build_mesh(
                    architectural_section,
                    architectural_sampler,
                    record.architectural_vertex_capacity_hints[section_index],
                    record.architectural_index_capacity_hints[section_index]);
                // Je transforme les descriptions de torches en primitives
                // arrondies après le maillage des surfaces, sans toucher à
                // leur cellule propriétaire ni à leur lumière logique.
                [[maybe_unused]] const auto fixture_index_offset =
                    append_architectural_fixture_geometry(
                    architectural_mesh,
                    StylizedPrimitiveLod::Medium);
                [[maybe_unused]] const auto prop_index_offset =
                    append_modern_backrooms_prop_geometry(
                        architectural_mesh,
                        architectural_section,
                        architectural_sampler,
                        StylizedPrimitiveLod::Medium);
            }
            record.architectural_vertex_capacity_hints[section_index] =
                std::max(
                    architectural_mesh.vertices.size(),
                    static_cast<std::size_t>(64));
            record.architectural_index_capacity_hints[section_index] =
                std::max(
                    architectural_mesh.indices.size(),
                    static_cast<std::size_t>(96));
            record.organic_vertex_capacity_hints[section_index] =
                std::max(
                    visual_mesh.vertices.size(),
                    static_cast<std::size_t>(128));
            record.organic_index_capacity_hints[section_index] =
                std::max(
                    visual_mesh.indices.size(),
                    static_cast<std::size_t>(192));
        } else {
            record.organic_section_meshes[section_index] = {};
            record.organic_vertex_capacity_hints[section_index] = 0U;
            record.organic_index_capacity_hints[section_index] = 0U;
            record.architectural_section_meshes[section_index] = {};
            record.architectural_vertex_capacity_hints[section_index] = 0U;
            record.architectural_index_capacity_hints[section_index] = 0U;
        }
        record.chunk.clear_section_dirty(section_index);
        break;
    }

    if (record.chunk.is_dirty()) {
        return false;
    }

    // Les sections sont la source de verite. Je libere l'ancien agregat et je
    // ne le reconstruis que si un consommateur de compatibilite appelle mesh_for().
    record.mesh = ChunkMeshData {};
    record.mesh_cache_dirty = true;
    ++record.mesh_revision;
    enqueue_gpu_upload(record.chunk.coord());
    return true;
}

auto World::visual_mesh_source_revision(const ChunkCoord& coord) const noexcept
    -> std::uint64_t {
    auto revision = std::uint64_t {14695981039346656037ULL};
    mix_revision(
        revision,
        static_cast<std::uint64_t>(
            static_cast<std::uint8_t>(visual_pipeline_)));
    mix_revision(
        revision,
        static_cast<std::uint64_t>(
            static_cast<std::uint32_t>(coord.x)));
    mix_revision(
        revision,
        static_cast<std::uint64_t>(
            static_cast<std::uint32_t>(coord.z)));

    // Je signe le chunk propriétaire et son halo 3x3. Un voisin chargé,
    // déchargé ou modifié rend donc immédiatement le staging obsolète.
    for (int offset_z = -1; offset_z <= 1; ++offset_z) {
        for (int offset_x = -1; offset_x <= 1; ++offset_x) {
            const ChunkCoord sampled_coord {
                coord.x + offset_x,
                coord.z + offset_z,
            };
            const auto iterator = chunks_.find(sampled_coord);
            if (iterator == chunks_.end()) {
                mix_revision(revision, 0U);
                continue;
            }
            mix_revision(revision, 1U);
            mix_revision(revision, iterator->second.chunk.mesh_input_revision());
        }
    }
    return revision;
}

void World::begin_modern_visual_remesh(ChunkRecord& record) {
    auto state = std::make_unique<ModernVisualRemeshState>();
    state->source_revision =
        visual_mesh_source_revision(record.chunk.coord());

    for (std::size_t section_index = 0U;
         section_index < kChunkSectionCount;
         ++section_index) {
        if (record.chunk.is_section_dirty(section_index)) {
            state->target_sections.set(section_index);
        }
    }

    const auto coord = record.chunk.coord();
    const auto chunk_world_x = coord.x * kChunkSizeX;
    const auto chunk_world_z = coord.z * kChunkSizeZ;
    // Je n'interprète jamais un bloc de bois placé dans les Backrooms comme
    // le tronc d'un arbre. Ce profil ne génère aucune végétation naturelle :
    // le bois y reste une architecture et évite un classement canonique de
    // tout le volume 3D au premier bloc décoratif rencontré.
    const auto has_current_vegetation =
        generation_profile() !=
            WorldGenerationProfile::Backrooms &&
        chunk_has_visual_vegetation(record.chunk);
    if (has_current_vegetation) {
        // Je classe le chunk sur toute sa hauteur une seule fois. Les
        // frontieres de sections ne peuvent donc plus couper un composant,
        // tronquer un cactus ou faire perdre le feuillage qui le qualifie.
        state->canonical_vegetation =
            build_visual_vegetation(
                {
                    {
                        chunk_world_x,
                        kWorldMinY,
                        chunk_world_z,
                    },
                    {
                        chunk_world_x + kChunkSizeX - 1,
                        kWorldMaxY,
                        chunk_world_z + kChunkSizeZ - 1,
                    },
                    kCanonicalVisualVegetationHalo,
                },
                [this](int x, int y, int z) {
                    return peek_block_or_generated(x, y, z);
                },
                static_cast<std::uint32_t>(seed()));

        const auto canonical_mesh =
            build_visual_vegetation_mesh(
                *state->canonical_vegetation,
                VisualVegetationLod::Medium,
                StylizedPrimitiveLod::Low,
                [this](int x, int y, int z) {
                    return VisualVegetationLighting {
                        get_sky_light(x, y, z),
                        get_block_light(x, y, z),
                    };
                });
        const auto partitions =
            partition_visual_vegetation_mesh(
                canonical_mesh,
                kWorldMinY,
                kChunkSectionHeight,
                kChunkSectionCount);
        for (std::size_t section_index = 0U;
             section_index < kChunkSectionCount;
             ++section_index) {
            state->vegetation_section_meshes[section_index] =
                partitions[section_index];
            if (!partitions[section_index].empty()) {
                state->vegetation_sections.set(section_index);
            }
        }
    }
    if (has_current_vegetation ||
        record.published_vegetation_sections.any()) {
        // Une modification de composant peut changer la hauteur du tronc ou
        // de la canopée loin de la cellule éditée. Je republie toutes les
        // anciennes et nouvelles partitions, même lorsque la dernière source
        // vient d'être retirée, sans rescanner un volume désormais vide.
        state->target_sections |=
            record.published_vegetation_sections;
        state->target_sections |=
            state->vegetation_sections;
    }

    for (std::size_t section_index = 0U;
         section_index < kChunkSectionCount;
         ++section_index) {
        if (!state->target_sections.test(section_index)) {
            continue;
        }
        if (chunk_section_has_organic_surface(
                *this,
                record.chunk,
                section_index)) {
            state->organic_sections.set(section_index);
        }
        if (chunk_section_has_architecture(
                record.chunk,
                section_index)) {
            state->architectural_sections.set(section_index);
        }
    }

    state->total_slices =
        state->target_sections.count() *
        kModernVisualRemeshSlicesPerSection;
    while (state->next_slice < kModernVisualRemeshSliceCount &&
           !state->target_sections.test(
               state->next_slice /
               kModernVisualRemeshSlicesPerSection)) {
        ++state->next_slice;
    }
    record.modern_remesh = std::move(state);
}

void World::build_modern_visual_remesh_slice(
    ChunkRecord& record,
    ModernVisualRemeshState& state,
    std::size_t slice_index) {
    const auto section_index =
        slice_index / kModernVisualRemeshSlicesPerSection;
    const auto slice_in_section =
        slice_index % kModernVisualRemeshSlicesPerSection;
    const auto min_y =
        static_cast<int>(slice_index *
                         static_cast<std::size_t>(
                             kModernVisualRemeshSliceHeight));
    const auto max_y =
        std::min(
            kWorldMaxY,
            min_y + kModernVisualRemeshSliceHeight - 1);
    const auto coord = record.chunk.coord();
    const auto chunk_world_x = coord.x * kChunkSizeX;
    const auto chunk_world_z = coord.z * kChunkSizeZ;

    if (slice_in_section == 0U) {
        state.staged_section_meshes[section_index] = {};
        state.staged_organic_meshes[section_index] = {};
        state.staged_architectural_meshes[section_index] = {};

        if (state.vegetation_sections.test(section_index)) {
            append_organic_mesh(
                state.staged_organic_meshes[section_index],
                state.vegetation_section_meshes[section_index]);
        }
    }

    const auto slice_mesh = mesher_.build_mesh_range(
        *this,
        coord,
        min_y,
        max_y,
        std::max<std::size_t>(
            32U,
            record.section_mesh_vertex_capacity_hints[section_index] /
                kModernVisualRemeshSlicesPerSection),
        std::max<std::size_t>(
            48U,
            record.section_mesh_index_capacity_hints[section_index] /
                kModernVisualRemeshSlicesPerSection),
        ChunkMeshContent::ModernNonOrganic);
    append_chunk_mesh_section(
        state.staged_section_meshes[section_index],
        slice_mesh);

    const OrganicTerrainSection slice_section {
        {chunk_world_x, min_y, chunk_world_z},
        {
            chunk_world_x + kChunkSizeX - 1,
            max_y,
            chunk_world_z + kChunkSizeZ - 1,
        },
    };
    if (state.organic_sections.test(section_index)) {
        const auto organic_slice = organic_mesher_.build_mesh(
            slice_section,
            [this](int x, int y, int z) {
                return OrganicTerrainCellSample {
                    peek_block_or_generated(x, y, z),
                    get_sky_light(x, y, z),
                    get_block_light(x, y, z),
                };
            },
            std::max<std::size_t>(
                32U,
                record.organic_vertex_capacity_hints[section_index] /
                    kModernVisualRemeshSlicesPerSection),
            std::max<std::size_t>(
                48U,
                record.organic_index_capacity_hints[section_index] /
                    kModernVisualRemeshSlicesPerSection));
        append_organic_mesh(
            state.staged_organic_meshes[section_index],
            organic_slice);
    }

    if (state.architectural_sections.test(section_index)) {
        auto architectural_slice = architectural_mesher_.build_mesh(
            {
                slice_section.min,
                slice_section.max,
                1,
            },
            [this, &state](int x, int y, int z) {
                auto block = peek_block_or_generated(x, y, z);
                if (state.canonical_vegetation.has_value() &&
                    coordinate_is_tree_wood(
                        *state.canonical_vegetation,
                        {x, y, z},
                        block)) {
                    block = to_block_id(BlockType::Air);
                }
                return ArchitecturalCellSample {
                    block,
                    get_sky_light(x, y, z),
                    get_block_light(x, y, z),
                };
            },
            std::max<std::size_t>(
                16U,
                record.architectural_vertex_capacity_hints[section_index] /
                    kModernVisualRemeshSlicesPerSection),
            std::max<std::size_t>(
                24U,
                record.architectural_index_capacity_hints[section_index] /
                    kModernVisualRemeshSlicesPerSection));
        [[maybe_unused]] const auto fixture_index_offset =
            append_architectural_fixture_geometry(
                architectural_slice,
                StylizedPrimitiveLod::Medium);
        append_architectural_mesh(
            state.staged_architectural_meshes[section_index],
            architectural_slice);
    }

}

void World::publish_modern_visual_remesh(ChunkRecord& record) {
    auto& state = *record.modern_remesh;
    for (std::size_t section_index = 0U;
         section_index < kChunkSectionCount;
         ++section_index) {
        if (!state.target_sections.test(section_index)) {
            continue;
        }

        record.section_meshes[section_index] =
            std::move(state.staged_section_meshes[section_index]);
        record.organic_section_meshes[section_index] =
            std::move(state.staged_organic_meshes[section_index]);
        record.architectural_section_meshes[section_index] =
            std::move(state.staged_architectural_meshes[section_index]);
        if (state.architectural_sections.test(section_index)) {
            const auto coord = record.chunk.coord();
            const ArchitecturalSection section {
                {
                    coord.x * kChunkSizeX,
                    section_min_y(section_index),
                    coord.z * kChunkSizeZ,
                },
                {
                    coord.x * kChunkSizeX + kChunkSizeX - 1,
                    section_max_y(section_index),
                    coord.z * kChunkSizeZ + kChunkSizeZ - 1,
                },
                1,
            };
            [[maybe_unused]] const auto prop_index_offset =
                append_modern_backrooms_prop_geometry(
                    record.architectural_section_meshes[section_index],
                    section,
                    [this, &state](int x, int y, int z) {
                        auto block = peek_block_or_generated(x, y, z);
                        if (state.canonical_vegetation.has_value() &&
                            coordinate_is_tree_wood(
                                *state.canonical_vegetation,
                                {x, y, z},
                                block)) {
                            block = to_block_id(BlockType::Air);
                        }
                        return ArchitecturalCellSample {
                            block,
                            get_sky_light(x, y, z),
                            get_block_light(x, y, z),
                        };
                    },
                    StylizedPrimitiveLod::Medium);
        }
        record.section_mesh_vertex_capacity_hints[section_index] =
            std::max<std::size_t>(
                128U,
                record.section_meshes[section_index].total_vertex_count());
        record.section_mesh_index_capacity_hints[section_index] =
            std::max<std::size_t>(
                192U,
                record.section_meshes[section_index].total_index_count());
        record.organic_vertex_capacity_hints[section_index] =
            std::max<std::size_t>(
                128U,
                record.organic_section_meshes[section_index].vertices.size());
        record.organic_index_capacity_hints[section_index] =
            std::max<std::size_t>(
                192U,
                record.organic_section_meshes[section_index].indices.size());
        record.architectural_vertex_capacity_hints[section_index] =
            std::max<std::size_t>(
                64U,
                record.architectural_section_meshes[section_index].vertices.size());
        record.architectural_index_capacity_hints[section_index] =
            std::max<std::size_t>(
                96U,
                record.architectural_section_meshes[section_index].indices.size());
        record.chunk.clear_section_dirty(section_index);
    }

    // Je bascule toutes les sections préparées avant de publier une seule
    // révision. Le renderer ne peut ainsi jamais observer un chunk hybride.
    record.published_vegetation_sections =
        state.vegetation_sections;
    record.mesh = ChunkMeshData {};
    record.mesh_cache_dirty = true;
    ++record.mesh_revision;
    record.modern_remesh.reset();
    enqueue_gpu_upload(record.chunk.coord());
}

auto World::rebuild_modern_chunk_mesh(ChunkRecord& record) -> bool {
    const auto current_revision =
        visual_mesh_source_revision(record.chunk.coord());
    if (record.modern_remesh != nullptr &&
        record.modern_remesh->source_revision != current_revision) {
        // Je jette sans publier le staging dont le halo ou la source a changé.
        record.modern_remesh.reset();
        return false;
    }

    if (record.modern_remesh == nullptr) {
        begin_modern_visual_remesh(record);
    }
    if (record.modern_remesh == nullptr ||
        record.modern_remesh->total_slices == 0U ||
        record.modern_remesh->next_slice >=
            kModernVisualRemeshSliceCount) {
        record.modern_remesh.reset();
        return false;
    }

    auto& state = *record.modern_remesh;
    const auto slice_index = state.next_slice;
    build_modern_visual_remesh_slice(record, state, slice_index);
    ++state.completed_slices;
    ++state.next_slice;
    while (state.next_slice < kModernVisualRemeshSliceCount &&
           !state.target_sections.test(
               state.next_slice /
               kModernVisualRemeshSlicesPerSection)) {
        ++state.next_slice;
    }

    if (visual_mesh_source_revision(record.chunk.coord()) !=
        state.source_revision) {
        record.modern_remesh.reset();
        return false;
    }
    if (state.completed_slices < state.total_slices) {
        return false;
    }

    publish_modern_visual_remesh(record);
    return true;
}

void World::rebuild_chunk_mesh_cache(const ChunkRecord& record) const {
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
    merged_mesh.vertices.reserve(
        std::max(record.mesh_vertex_capacity_hint, std::max<std::size_t>(merged_vertex_count, 256U)));
    merged_mesh.indices.reserve(
        std::max(record.mesh_index_capacity_hint, std::max<std::size_t>(merged_index_count, 384U)));
    merged_mesh.water_vertices.reserve(std::max<std::size_t>(merged_water_vertex_count, 128U));
    merged_mesh.water_indices.reserve(std::max<std::size_t>(merged_water_index_count, 192U));
    for (const auto& section_mesh : record.section_meshes) {
        append_chunk_mesh_section(merged_mesh, section_mesh);
    }

    record.mesh = std::move(merged_mesh);
    record.mesh_vertex_capacity_hint =
        std::max(record.mesh.total_vertex_count(), static_cast<std::size_t>(256));
    record.mesh_index_capacity_hint =
        std::max(record.mesh.total_index_count(), static_cast<std::size_t>(384));
    record.mesh_cache_dirty = false;
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

    auto refreshed_entry =
        make_chunk_override_entry(
            coord,
            chunk.blocks(),
            chunk.water_state(),
            iterator->second.player_placed_mask);
    if (!refreshed_entry.has_value()) {
        chunk_overrides_.erase(iterator);
        return;
    }
    iterator->second = std::move(*refreshed_entry);
}

void World::apply_chunk_override_to_record(ChunkRecord& record, const ChunkOverrideEntry& entry) {
    const auto snapshot = materialize_chunk_override(record.chunk.coord(), entry);
    record.chunk.copy_blocks_from(snapshot.blocks.data(), snapshot.blocks.size());
    record.chunk.copy_water_from(snapshot.water_state.data(), snapshot.water_state.size());
    record.chunk.clear_lighting();
    record.sky_columns_dirty.set();
    refresh_chunk_emissive_cache(record);
}

auto World::make_chunk_override_entry(
    const ChunkCoord& coord,
    const std::array<BlockId, kChunkVolume>& blocks,
    const std::array<WaterState, kChunkVolume>& water_state,
    const WorldPlayerPlacedMask& player_placed_mask) const
    -> std::optional<ChunkOverrideEntry> {
    auto generated_state = generator_.begin_chunk_generation(coord);
    generator_.advance_chunk_generation(
        generated_state,
        static_cast<std::size_t>(kChunkSizeX * kChunkSizeZ));
    const auto& generated_chunk = generated_state.chunk;
    const auto& generated_blocks = generated_chunk.blocks();
    const auto& generated_water = generated_chunk.water_state();

    ChunkOverrideEntry entry {};
    entry.player_placed_mask =
        player_placed_mask;
    entry.player_placed_count =
        player_placed_mask_count(
            player_placed_mask);
    entry.sparse_cells.reserve(std::min<std::size_t>(64U, kSparseOverrideCellLimit));
    auto normalized_water = water_state;
    for (std::size_t block_index = 0; block_index < kChunkVolume; ++block_index) {
        if (water_level_from_state(normalized_water[block_index]) == kMaxWaterLevel &&
            water_state_is_source(normalized_water[block_index]) &&
            water_state_is_infinite(generated_water[block_index])) {
            normalized_water[block_index] = make_water_state(kMaxWaterLevel, true, true);
        }

        if (blocks[block_index] == generated_blocks[block_index] &&
            normalized_water[block_index] == generated_water[block_index]) {
            continue;
        }

        entry.changed_cells.set(block_index);
        ++entry.generator_mismatch_count;
        if (entry.generator_mismatch_count <= kSparseOverrideCellLimit) {
            entry.sparse_cells.push_back({
                static_cast<std::uint16_t>(block_index),
                blocks[block_index],
                normalized_water[block_index],
                generated_blocks[block_index],
                generated_water[block_index],
            });
        }
    }

    if (entry.generator_mismatch_count == 0U &&
        entry.player_placed_count == 0U) {
        return std::nullopt;
    }
    if (entry.generator_mismatch_count > kSparseOverrideCellLimit) {
        entry.sparse_cells.clear();
        entry.sparse_cells.shrink_to_fit();
        entry.dense = std::make_unique<DenseChunkOverride>();
        entry.dense->blocks = blocks;
        entry.dense->water_state = std::move(normalized_water);
        entry.dense->generated_blocks = generated_blocks;
        entry.dense->generated_water_state = generated_water;
    }
    return entry;
}

auto World::materialize_chunk_override(const ChunkCoord& coord, const ChunkOverrideEntry& entry) const
    -> WorldChunkSnapshot {
    WorldChunkSnapshot snapshot {};
    snapshot.coord = coord;
    snapshot.player_placed_mask =
        entry.player_placed_mask;
    if (entry.dense != nullptr) {
        snapshot.blocks = entry.dense->blocks;
        snapshot.water_state = entry.dense->water_state;
        return snapshot;
    }

    auto generated_state = generator_.begin_chunk_generation(coord);
    generator_.advance_chunk_generation(
        generated_state,
        static_cast<std::size_t>(kChunkSizeX * kChunkSizeZ));
    snapshot.blocks = generated_state.chunk.blocks();
    snapshot.water_state = generated_state.chunk.water_state();
    for (const auto& cell : entry.sparse_cells) {
        const auto block_index = static_cast<std::size_t>(cell.index);
        snapshot.blocks[block_index] = cell.block;
        snapshot.water_state[block_index] = cell.water_state;
    }
    return snapshot;
}

auto World::find_sparse_override_cell(ChunkOverrideEntry& entry, std::size_t block_index)
    -> std::vector<ChunkOverrideCell>::iterator {
    return std::lower_bound(
        entry.sparse_cells.begin(),
        entry.sparse_cells.end(),
        block_index,
        [](const ChunkOverrideCell& cell, std::size_t index) {
            return static_cast<std::size_t>(cell.index) < index;
        });
}

auto World::find_sparse_override_cell(const ChunkOverrideEntry& entry, std::size_t block_index) const
    -> std::vector<ChunkOverrideCell>::const_iterator {
    return std::lower_bound(
        entry.sparse_cells.begin(),
        entry.sparse_cells.end(),
        block_index,
        [](const ChunkOverrideCell& cell, std::size_t index) {
            return static_cast<std::size_t>(cell.index) < index;
        });
}

void World::set_chunk_override_cell(ChunkOverrideEntry& entry,
                                    std::size_t block_index,
                                    BlockId block,
                                    WaterState water_state,
                                    BlockId fallback_generated_block,
                                    WaterState fallback_generated_water_state,
                                    bool player_placed,
                                    const Chunk* loaded_chunk) {
    if (block_index >= kChunkVolume) {
        return;
    }
    auto generated_block = fallback_generated_block;
    auto generated_water_state = fallback_generated_water_state;
    auto sparse_cell = entry.sparse_cells.end();
    auto previously_mismatched = false;
    if (entry.dense != nullptr) {
        generated_block = entry.dense->generated_blocks[block_index];
        generated_water_state = entry.dense->generated_water_state[block_index];
        previously_mismatched =
            entry.dense->blocks[block_index] !=
                generated_block ||
            entry.dense->water_state[block_index] !=
                generated_water_state;
    } else {
        sparse_cell = find_sparse_override_cell(entry, block_index);
        if (sparse_cell != entry.sparse_cells.end() &&
            static_cast<std::size_t>(sparse_cell->index) == block_index) {
            generated_block = sparse_cell->generated_block;
            generated_water_state = sparse_cell->generated_water_state;
            previously_mismatched =
                sparse_cell->block !=
                    generated_block ||
                sparse_cell->water_state !=
                    generated_water_state;
        }
    }

    const auto mismatch = block != generated_block || water_state != generated_water_state;
    const auto was_player_placed =
        player_placed_mask_test(
            entry.player_placed_mask,
            block_index);

    if (entry.dense != nullptr) {
        entry.dense->blocks[block_index] = block;
        entry.dense->water_state[block_index] = water_state;
    } else {
        if (mismatch) {
            if (sparse_cell != entry.sparse_cells.end() &&
                static_cast<std::size_t>(sparse_cell->index) == block_index) {
                sparse_cell->block = block;
                sparse_cell->water_state = water_state;
            } else {
                entry.sparse_cells.insert(sparse_cell, {
                    static_cast<std::uint16_t>(block_index),
                    block,
                    water_state,
                    generated_block,
                    generated_water_state,
                });
            }
        } else if (sparse_cell != entry.sparse_cells.end() &&
                   static_cast<std::size_t>(sparse_cell->index) == block_index) {
            entry.sparse_cells.erase(sparse_cell);
        }
    }

    if (previously_mismatched && !mismatch) {
        entry.changed_cells.reset(block_index);
        --entry.generator_mismatch_count;
    } else if (!previously_mismatched && mismatch) {
        entry.changed_cells.set(block_index);
        ++entry.generator_mismatch_count;
    }

    if (was_player_placed != player_placed) {
        player_placed_mask_set(
            entry.player_placed_mask,
            block_index,
            player_placed);
        if (player_placed) {
            ++entry.player_placed_count;
        } else if (entry.player_placed_count > 0U) {
            --entry.player_placed_count;
        }
    }

    if (entry.dense == nullptr && entry.sparse_cells.size() > kSparseOverrideCellLimit && loaded_chunk != nullptr) {
        auto dense = std::make_unique<DenseChunkOverride>();
        dense->blocks = loaded_chunk->blocks();
        dense->water_state = loaded_chunk->water_state();
        dense->generated_blocks = dense->blocks;
        dense->generated_water_state = dense->water_state;
        for (const auto& cell : entry.sparse_cells) {
            const auto cell_index = static_cast<std::size_t>(cell.index);
            dense->generated_blocks[cell_index] = cell.generated_block;
            dense->generated_water_state[cell_index] = cell.generated_water_state;
        }
        entry.dense = std::move(dense);
        entry.sparse_cells.clear();
        entry.sparse_cells.shrink_to_fit();
    }
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

auto World::uses_static_poolrooms_water() const noexcept -> bool {
    return generator_.profile() ==
               WorldGenerationProfile::Backrooms &&
           (uses_backrooms_spatial_stack(
                generator_.generation_version()) ||
            generator_.backrooms_level() <= -2);
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
    std::unordered_set<BlockCoord, BlockCoordHash>& pressure_head_missing_cache) -> std::optional<WaterPressureHead> {
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

    fluid_pressure_frontier_.clear();
    fluid_pressure_visited_.clear();
    fluid_pressure_seen_.clear();
    auto& frontier = fluid_pressure_frontier_;
    auto& visited = fluid_pressure_visited_;
    auto& seen = fluid_pressure_seen_;

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
                                                    WaterState next_water_state,
                                                    bool mark_player_placed) {
    const auto block_index = chunk_linear_index(local_coord.x, local_coord.y, local_coord.z);

    if (auto override_iterator = chunk_overrides_.find(coord); override_iterator == chunk_overrides_.end()) {
        const auto mismatch =
            previous_block != next_block ||
            previous_water_state != next_water_state;
        if (!mismatch &&
            !mark_player_placed) {
            return;
        }
        ChunkOverrideEntry entry {};
        entry.sparse_cells.reserve(std::min<std::size_t>(64U, kSparseOverrideCellLimit));
        if (mismatch) {
            entry.changed_cells.set(block_index);
            entry.generator_mismatch_count = 1U;
            entry.sparse_cells.push_back({
                static_cast<std::uint16_t>(block_index),
                next_block,
                next_water_state,
                previous_block,
                previous_water_state,
            });
        }
        if (mark_player_placed) {
            player_placed_mask_set(
                entry.player_placed_mask,
                block_index,
                true);
            entry.player_placed_count = 1U;
        }
        chunk_overrides_.emplace(coord, std::move(entry));
    } else {
        auto& entry = override_iterator->second;
        const auto player_placed =
            mark_player_placed ||
            player_placed_mask_test(
                entry.player_placed_mask,
                block_index);
        set_chunk_override_cell(
            entry,
            block_index,
            next_block,
            next_water_state,
            previous_block,
            previous_water_state,
            player_placed,
            find_chunk(coord));
        if (entry.generator_mismatch_count == 0U &&
            entry.player_placed_count == 0U) {
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
    return dx <= active_stream_radius_ && dz <= active_stream_radius_;
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
