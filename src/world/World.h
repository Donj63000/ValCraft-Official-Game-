#pragma once

#include "world/Chunk.h"
#include "world/ChunkMesher.h"
#include "world/WorldGenerator.h"

#include <glm/vec3.hpp>

#include <array>
#include <bitset>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <optional>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace valcraft {

struct WorldWorkBudget {
    std::size_t chunk_generation_budget = 2;
    std::size_t mesh_rebuild_budget = 4;
    std::size_t light_node_budget = 16384;
    double max_generation_ms = 1.0;
    double max_lighting_ms = 1.5;
    double max_meshing_ms = 2.0;
};

struct WorldWorkStats {
    std::size_t generated_chunks = 0;
    std::size_t meshed_chunks = 0;
    std::size_t prioritized_meshed_chunks = 0;
    std::size_t light_nodes_processed = 0;
    std::size_t lighting_jobs_completed = 0;
    std::size_t pending_generation = 0;
    std::size_t pending_mesh = 0;
    std::size_t pending_lighting = 0;
    double generation_ms = 0.0;
    double lighting_ms = 0.0;
    double meshing_ms = 0.0;
};

struct WorldStreamingStats {
    bool chunk_changed = false;
    std::size_t generation_enqueued = 0;
    std::size_t generation_pruned = 0;
    std::size_t unloaded_chunks = 0;
};

class World {
public:
    struct ChunkRecord {
        explicit ChunkRecord(ChunkCoord coord)
            : chunk(coord) {
            sky_columns_dirty.set();
        }

        Chunk chunk;
        ChunkMeshData mesh {};
        std::array<ChunkMeshData, kChunkSectionCount> section_meshes {};
        std::uint64_t mesh_revision = 0;
        std::vector<BlockCoord> emissive_blocks {};
        std::bitset<kChunkSizeX * kChunkSizeZ> sky_columns_dirty {};
        std::size_t mesh_vertex_capacity_hint = 0;
        std::size_t mesh_index_capacity_hint = 0;
        std::array<std::size_t, kChunkSectionCount> section_mesh_vertex_capacity_hints {};
        std::array<std::size_t, kChunkSectionCount> section_mesh_index_capacity_hints {};
    };

    explicit World(int seed = 1337, int stream_radius = kDefaultStreamRadius);

    [[nodiscard]] auto get_block(int x, int y, int z) const -> BlockId;
    [[nodiscard]] auto get_sky_light(int x, int y, int z) const -> std::uint8_t;
    [[nodiscard]] auto get_block_light(int x, int y, int z) const -> std::uint8_t;
    void set_block(int x, int y, int z, BlockId block_id);

    [[nodiscard]] auto world_to_chunk(int x, int z) const noexcept -> ChunkCoord;
    [[nodiscard]] auto world_to_local(int x, int y, int z) const noexcept -> BlockCoord;
    [[nodiscard]] auto local_to_world(const ChunkCoord& chunk_coord, const BlockCoord& local) const noexcept -> BlockCoord;
    [[nodiscard]] auto raycast(const glm::vec3& origin, const glm::vec3& direction, float max_distance) const -> RaycastHit;
    [[nodiscard]] auto can_place_torch_at(const BlockCoord& world_coord) const -> bool;

    void ensure_chunk_loaded(const ChunkCoord& coord);
    auto update_streaming(const glm::vec3& player_position) -> WorldStreamingStats;
    [[nodiscard]] auto process_pending_work(const WorldWorkBudget& budget = {}) -> WorldWorkStats;
    void rebuild_lighting();
    void rebuild_dirty_meshes();

    [[nodiscard]] auto find_chunk(const ChunkCoord& coord) -> Chunk*;
    [[nodiscard]] auto find_chunk(const ChunkCoord& coord) const -> const Chunk*;
    [[nodiscard]] auto mesh_for(const ChunkCoord& coord) const -> const ChunkMeshData*;
    [[nodiscard]] auto mesh_revision(const ChunkCoord& coord) const -> std::uint64_t;
    [[nodiscard]] auto chunk_records() const noexcept -> const std::unordered_map<ChunkCoord, ChunkRecord, ChunkCoordHash>&;
    [[nodiscard]] auto consume_pending_gpu_uploads(std::size_t max_count) -> std::vector<ChunkCoord>;
    [[nodiscard]] auto consume_pending_gpu_unloads(std::size_t max_count) -> std::vector<ChunkCoord>;

    [[nodiscard]] auto seed() const noexcept -> int;
    [[nodiscard]] auto stream_radius() const noexcept -> int;
    [[nodiscard]] auto surface_height(int world_x, int world_z) -> int;
    [[nodiscard]] auto loaded_surface_height(int world_x, int world_z) const -> std::optional<int>;
    [[nodiscard]] auto pending_generation_count() const noexcept -> std::size_t;
    [[nodiscard]] auto pending_mesh_count() const noexcept -> std::size_t;
    [[nodiscard]] auto pending_lighting_count() const noexcept -> std::size_t;
    [[nodiscard]] auto has_pending_work() const noexcept -> bool;
    [[nodiscard]] auto are_chunks_ready(const glm::vec3& player_position, int radius) const -> bool;

    [[nodiscard]] static auto floor_div(int value, int divisor) noexcept -> int;
    [[nodiscard]] static auto positive_mod(int value, int divisor) noexcept -> int;

private:
    static constexpr std::size_t kLightingRegionSlots = 5;

    struct PendingLightingUpdate {
        ChunkCoord anchor {};
        std::vector<ChunkCoord> coverage;
    };

    struct LightNode {
        BlockCoord world_coord {};
        std::uint8_t light_level = 0;
    };

    struct LightingJob {
        ChunkCoord anchor {};
        std::array<ChunkCoord, kLightingRegionSlots> region_coords {};
        std::array<bool, kLightingRegionSlots> region_present {};
        std::array<std::vector<std::uint8_t>, kLightingRegionSlots> block_light_buffers {};
        std::array<std::bitset<kChunkSizeX * kChunkSizeZ>, kLightingRegionSlots> processed_sky_columns {};
        std::array<bool, kLightingRegionSlots> changed_chunks {};
        std::array<std::size_t, kLightingRegionSlots> block_light_difference_counts {};
        std::deque<LightNode> queue;
    };

    void mark_chunk_and_neighbors_dirty(const ChunkCoord& coord, const BlockCoord& local);
    void mark_neighbors_dirty(const ChunkCoord& coord);
    void mark_chunk_and_neighbors_lighting_dirty(const ChunkCoord& coord);
    void mark_sky_column_dirty(const ChunkCoord& coord, int local_x, int local_z);
    void load_chunk_immediate(const ChunkCoord& coord);
    void enqueue_generation_candidate(const ChunkCoord& coord, WorldStreamingStats* stats = nullptr);
    void enqueue_generation_area(const ChunkCoord& center, WorldStreamingStats& stats);
    void enqueue_generation_ring_transition(const ChunkCoord& previous_center, const ChunkCoord& next_center, WorldStreamingStats& stats);
    void prune_generation_queue(WorldStreamingStats& stats);
    void enqueue_lighting_update(const ChunkCoord& coord);
    void invalidate_loaded_mesh_neighbors(const ChunkCoord& coord, bool defer_if_lighting_pending);
    void invalidate_loaded_mesh_neighbors_for_sections(
        const ChunkCoord& coord,
        const std::array<bool, kChunkSectionCount>& dirty_sections);
    void invalidate_loaded_mesh_neighbors_for_chunk_load(const ChunkCoord& coord);
    void rebuild_pending_lighting_metadata();
    void flush_deferred_mesh_invalidations();
    void enqueue_mesh_rebuild(const ChunkCoord& coord, bool prioritize = false);
    void enqueue_dirty_chunks();
    void process_generation_queue(std::size_t budget, double max_ms, WorldWorkStats& stats);
    void process_lighting_queue(std::size_t budget, double max_ms, WorldWorkStats& stats);
    void process_mesh_queue(std::size_t budget, double max_ms, WorldWorkStats& stats);
    [[nodiscard]] auto collect_lighting_region(const ChunkCoord& anchor) const -> std::vector<ChunkCoord>;
    [[nodiscard]] auto initialize_lighting_job(LightingJob& job) -> bool;
    void rebuild_local_sky_light(LightingJob& job);
    void seed_local_block_lighting(LightingJob& job);
    void finalize_lighting_job(const LightingJob& job);
    [[nodiscard]] auto unload_far_chunks(const ChunkCoord& center) -> std::size_t;
    void rebuild_chunk_mesh(ChunkRecord& record);
    void enqueue_gpu_upload(const ChunkCoord& coord);
    void enqueue_gpu_unload(const ChunkCoord& coord);
    void remove_unsupported_torch_above(int x, int y, int z);
    void refresh_chunk_emissive_cache(ChunkRecord& record);
    void update_chunk_emissive_cache(ChunkRecord& record, const BlockCoord& local_coord, BlockId previous_block, BlockId next_block);
    [[nodiscard]] auto lighting_region_contains(const LightingJob& job, const ChunkCoord& coord) const noexcept -> bool;
    void mark_lighting_chunk_changed(LightingJob& job, const ChunkCoord& coord);
    [[nodiscard]] auto lighting_buffer_index(const BlockCoord& local_coord) const noexcept -> std::size_t;
    [[nodiscard]] auto get_job_block_light(const LightingJob& job, const BlockCoord& world_coord) const -> std::uint8_t;
    [[nodiscard]] auto set_job_block_light(LightingJob& job, const BlockCoord& world_coord, std::uint8_t light_level) -> bool;
    [[nodiscard]] auto is_inside_active_stream(const ChunkCoord& coord) const noexcept -> bool;
    [[nodiscard]] auto should_prioritize_mesh_invalidation(const ChunkCoord& coord) const noexcept -> bool;
    [[nodiscard]] auto chunk_has_pending_lighting(const ChunkCoord& coord) const noexcept -> bool;
    [[nodiscard]] auto lighting_anchor_affects(const ChunkCoord& target, const ChunkCoord& anchor) const noexcept -> bool;

    int stream_radius_ = kDefaultStreamRadius;
    WorldGenerator generator_;
    ChunkMesher mesher_;
    std::unordered_map<ChunkCoord, ChunkRecord, ChunkCoordHash> chunks_ {};
    std::deque<ChunkCoord> pending_generation_queue_ {};
    std::unordered_set<ChunkCoord, ChunkCoordHash> pending_generation_set_ {};
    std::deque<ChunkCoord> pending_priority_mesh_queue_ {};
    std::deque<ChunkCoord> pending_mesh_queue_ {};
    std::deque<PendingLightingUpdate> pending_lighting_queue_ {};
    std::unordered_set<ChunkCoord, ChunkCoordHash> pending_mesh_set_ {};
    std::unordered_set<ChunkCoord, ChunkCoordHash> pending_priority_mesh_set_ {};
    std::unordered_set<ChunkCoord, ChunkCoordHash> deferred_mesh_invalidation_set_ {};
    std::unordered_set<ChunkCoord, ChunkCoordHash> pending_lighting_set_ {};
    std::unordered_set<ChunkCoord, ChunkCoordHash> pending_lighting_coverage_ {};
    std::unordered_set<ChunkCoord, ChunkCoordHash> active_lighting_coverage_ {};
    std::deque<ChunkCoord> pending_gpu_uploads_ {};
    std::deque<ChunkCoord> pending_gpu_unloads_ {};
    std::unordered_set<ChunkCoord, ChunkCoordHash> pending_gpu_upload_set_ {};
    std::unordered_set<ChunkCoord, ChunkCoordHash> pending_gpu_unload_set_ {};
    std::optional<LightingJob> active_lighting_job_ {};
    ChunkCoord stream_center_ {};
    bool has_stream_center_ = false;
};

} // namespace valcraft
