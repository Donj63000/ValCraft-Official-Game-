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
#include <utility>
#include <vector>

namespace valcraft {

struct WorldWorkBudget {
    std::size_t chunk_generation_budget = 2;
    std::size_t fluid_cell_budget = 256;
    std::size_t mesh_rebuild_budget = 4;
    std::size_t light_node_budget = 16384;
    double max_generation_ms = 1.0;
    double max_fluid_ms = 1.0;
    double max_lighting_ms = 1.5;
    double max_meshing_ms = 2.0;
};

struct WorldWorkStats {
    std::size_t generated_chunks = 0;
    std::size_t processed_fluid_cells = 0;
    std::size_t meshed_chunks = 0;
    std::size_t mesh_sections_processed = 0;
    std::size_t prioritized_meshed_chunks = 0;
    std::size_t light_nodes_processed = 0;
    std::size_t lighting_work_units_processed = 0;
    std::size_t lighting_jobs_started = 0;
    std::size_t lighting_jobs_completed = 0;
    std::size_t fluid_cells_changed = 0;
    std::size_t pending_generation = 0;
    std::size_t pending_fluid = 0;
    std::size_t pending_mesh = 0;
    std::size_t pending_lighting = 0;
    double generation_ms = 0.0;
    double fluid_ms = 0.0;
    double lighting_ms = 0.0;
    double lighting_setup_ms = 0.0;
    double lighting_finalize_ms = 0.0;
    double meshing_ms = 0.0;
};

struct WorldMemoryStats {
    std::size_t loaded_chunks = 0;
    std::size_t override_chunks = 0;
    std::size_t chunk_cpu_bytes = 0;
    std::size_t mesh_cpu_bytes = 0;
    std::size_t override_bytes = 0;
    std::size_t fluid_cpu_bytes = 0;
    std::size_t lighting_cpu_bytes = 0;
    std::size_t generation_cpu_bytes = 0;
    std::size_t queue_cpu_bytes = 0;
    std::size_t world_cpu_bytes = 0;
    std::size_t mesh_vertex_capacity = 0;
    std::size_t mesh_index_capacity = 0;
};

struct WorldStreamingStats {
    bool chunk_changed = false;
    std::size_t generation_enqueued = 0;
    std::size_t generation_pruned = 0;
    std::size_t unloaded_chunks = 0;
};

enum class WorldRaycastMode : std::uint8_t {
    Selection = 0,
    VisibilityOpaque = 1,
    ProjectileCollidable = 2,
};

struct WorldChunkSnapshot {
    ChunkCoord coord {};
    std::array<BlockId, kChunkVolume> blocks {};
    std::array<WaterState, kChunkVolume> water_state {};
};

struct WorldSavePlanCell {
    std::uint16_t index = 0;
    BlockId block = to_block_id(BlockType::Air);
    WaterState water_state = 0;
};

struct WorldSavePlanChunk {
    ChunkCoord coord {};
    std::vector<WorldSavePlanCell> sparse_cells {};
    std::vector<BlockId> dense_blocks {};
    std::vector<WaterState> dense_water_state {};

    [[nodiscard]] auto dense() const noexcept -> bool {
        return !dense_blocks.empty();
    }
};

struct WorldSavePlan {
    int seed = 1337;
    WorldGenerationProfile generation_profile = WorldGenerationProfile::Continental;
    WorldGenerationVersion generation_version = WorldGenerationVersion::LegacyV1;
    std::vector<WorldSavePlanChunk> chunks {};
};

struct WorldSaveRestoreStats {
    std::size_t processed_cells = 0;
    std::size_t completed_chunks = 0;
    std::size_t pending_cells = 0;
    float progress = 1.0F;
};

class World {
public:
    struct ChunkRecord {
        explicit ChunkRecord(ChunkCoord coord)
            : chunk(coord) {
            sky_columns_dirty.set();
        }

        explicit ChunkRecord(Chunk&& generated_chunk)
            : chunk(std::move(generated_chunk)) {
            sky_columns_dirty.set();
        }

        Chunk chunk;
        mutable ChunkMeshData mesh {};
        std::array<ChunkMeshData, kChunkSectionCount> section_meshes {};
        mutable bool mesh_cache_dirty = false;
        std::uint64_t mesh_revision = 0;
        std::vector<BlockCoord> emissive_blocks {};
        std::bitset<kChunkSizeX * kChunkSizeZ> sky_columns_dirty {};
        mutable std::size_t mesh_vertex_capacity_hint = 0;
        mutable std::size_t mesh_index_capacity_hint = 0;
        std::array<std::size_t, kChunkSectionCount> section_mesh_vertex_capacity_hints {};
        std::array<std::size_t, kChunkSectionCount> section_mesh_index_capacity_hints {};
    };

    explicit World(int seed = 1337,
                   int stream_radius = kDefaultStreamRadius,
                   WorldGenerationProfile generation_profile = WorldGenerationProfile::Continental,
                   WorldGenerationVersion generation_version = WorldGenerationVersion::Latest);

    [[nodiscard]] auto get_block(int x, int y, int z) const -> BlockId;
    [[nodiscard]] auto water_level(int x, int y, int z) const -> std::uint8_t;
    [[nodiscard]] auto has_water(int x, int y, int z) const -> bool;
    [[nodiscard]] auto water_surface_y(int x, int y, int z) const -> std::optional<float>;
    // Helper de maillage: si le chunk n'est pas encore charge, on previsualise
    // le terrain/eau deterministe issu du generateur pour eviter les murs d'eau
    // temporaires sur les frontieres de streaming.
    [[nodiscard]] auto peek_block_or_generated(int x, int y, int z) const -> BlockId;
    [[nodiscard]] auto peek_water_level_or_generated(int x, int y, int z) const -> std::uint8_t;
    [[nodiscard]] auto get_sky_light(int x, int y, int z) const -> std::uint8_t;
    [[nodiscard]] auto get_block_light(int x, int y, int z) const -> std::uint8_t;
    void set_block(int x, int y, int z, BlockId block_id);
    // Je restaure simultanement le bloc et l'etat d'eau procedural, y compris
    // le drapeau de source infinie que set_block(Water) ne peut pas exprimer.
    [[nodiscard]] auto restore_generated_cell(int x, int y, int z) -> bool;

    [[nodiscard]] auto world_to_chunk(int x, int z) const noexcept -> ChunkCoord;
    [[nodiscard]] auto world_to_local(int x, int y, int z) const noexcept -> BlockCoord;
    [[nodiscard]] auto local_to_world(const ChunkCoord& chunk_coord, const BlockCoord& local) const noexcept -> BlockCoord;
    [[nodiscard]] auto raycast(const glm::vec3& origin, const glm::vec3& direction, float max_distance) const -> RaycastHit;
    [[nodiscard]] auto raycast(const glm::vec3& origin,
                               const glm::vec3& direction,
                               float max_distance,
                               WorldRaycastMode mode) const -> RaycastHit;
    [[nodiscard]] auto raycast_visibility(const glm::vec3& origin,
                                          const glm::vec3& direction,
                                          float max_distance) const -> RaycastHit;
    [[nodiscard]] auto raycast_collidable(const glm::vec3& origin,
                                          const glm::vec3& direction,
                                          float max_distance) const -> RaycastHit;
    [[nodiscard]] auto can_place_torch_at(const BlockCoord& world_coord) const -> bool;
    [[nodiscard]] auto can_place_torch_at(const BlockCoord& world_coord, const BlockCoord& support_coord) const -> bool;
    [[nodiscard]] auto torch_block_to_place(const BlockCoord& world_coord, const BlockCoord& support_coord) const
        -> std::optional<BlockId>;

    void ensure_chunk_loaded(const ChunkCoord& coord);
    auto update_streaming(const glm::vec3& player_position) -> WorldStreamingStats;
    auto update_streaming(const glm::vec3& player_position, int requested_radius) -> WorldStreamingStats;
    [[nodiscard]] auto process_pending_work(const WorldWorkBudget& budget = {}) -> WorldWorkStats;
    void rebuild_lighting();
    void rebuild_dirty_meshes();

    [[nodiscard]] auto find_chunk(const ChunkCoord& coord) -> Chunk*;
    [[nodiscard]] auto find_chunk(const ChunkCoord& coord) const -> const Chunk*;
    [[nodiscard]] auto mesh_for(const ChunkCoord& coord) const -> const ChunkMeshData*;
    [[nodiscard]] auto section_meshes_for(const ChunkCoord& coord) const
        -> const std::array<ChunkMeshData, kChunkSectionCount>*;
    [[nodiscard]] auto mesh_revision(const ChunkCoord& coord) const -> std::uint64_t;
    [[nodiscard]] auto chunk_records() const noexcept -> const std::unordered_map<ChunkCoord, ChunkRecord, ChunkCoordHash>&;
    void enqueue_loaded_mesh_uploads();
    [[nodiscard]] auto consume_pending_gpu_uploads(std::size_t max_count) -> std::vector<ChunkCoord>;
    [[nodiscard]] auto consume_pending_gpu_unloads(std::size_t max_count) -> std::vector<ChunkCoord>;
    [[nodiscard]] auto pending_gpu_upload_count() const noexcept -> std::size_t;

    [[nodiscard]] auto seed() const noexcept -> int;
    [[nodiscard]] auto generation_profile() const noexcept -> WorldGenerationProfile;
    [[nodiscard]] auto generation_version() const noexcept -> WorldGenerationVersion;
    [[nodiscard]] auto stream_radius() const noexcept -> int;
    [[nodiscard]] auto surface_height(int world_x, int world_z) -> int;
    [[nodiscard]] auto loaded_surface_height(int world_x, int world_z) const -> std::optional<int>;
    [[nodiscard]] auto pending_generation_count() const noexcept -> std::size_t;
    [[nodiscard]] auto pending_fluid_count() const noexcept -> std::size_t;
    [[nodiscard]] auto pending_mesh_count() const noexcept -> std::size_t;
    [[nodiscard]] auto pending_lighting_count() const noexcept -> std::size_t;
    [[nodiscard]] auto memory_stats() const noexcept -> WorldMemoryStats;
    [[nodiscard]] auto has_pending_work() const noexcept -> bool;
    [[nodiscard]] auto are_chunks_ready(const glm::vec3& player_position, int radius) const -> bool;
    [[nodiscard]] auto modified_chunk_snapshots() const -> std::vector<WorldChunkSnapshot>;
    [[nodiscard]] auto capture_save_plan() const -> WorldSavePlan;
    void begin_restore_save_plan(WorldSavePlan plan);
    [[nodiscard]] auto process_save_restore(std::size_t cell_budget, double max_ms) -> WorldSaveRestoreStats;
    [[nodiscard]] auto has_pending_save_restore() const noexcept -> bool;
    [[nodiscard]] auto save_restore_progress() const noexcept -> float;
    void replace_chunk_snapshots(const std::vector<WorldChunkSnapshot>& snapshots);

    [[nodiscard]] static auto floor_div(int value, int divisor) noexcept -> int;
    [[nodiscard]] static auto positive_mod(int value, int divisor) noexcept -> int;

private:
    static constexpr std::size_t kLightingRegionSlots = 5;
    static constexpr std::size_t kSparseOverrideCellLimit = 2048;

    struct ChunkOverrideCell {
        std::uint16_t index = 0;
        BlockId block = to_block_id(BlockType::Air);
        WaterState water_state = 0;
        BlockId generated_block = to_block_id(BlockType::Air);
        WaterState generated_water_state = 0;
    };

    struct DenseChunkOverride {
        std::array<BlockId, kChunkVolume> blocks {};
        std::array<WaterState, kChunkVolume> water_state {};
        std::array<BlockId, kChunkVolume> generated_blocks {};
        std::array<WaterState, kChunkVolume> generated_water_state {};
    };

    struct ChunkOverrideEntry {
        std::bitset<kChunkVolume> changed_cells {};
        std::vector<ChunkOverrideCell> sparse_cells {};
        std::unique_ptr<DenseChunkOverride> dense {};
        std::size_t generator_mismatch_count = 0;
    };

    struct SaveRestoreState {
        WorldSavePlan plan {};
        std::size_t next_chunk = 0;
        std::size_t next_cell = 0;
        std::size_t processed_cells = 0;
        std::size_t total_cells = 0;
        std::unique_ptr<WorldGenerator::ChunkGenerationState> generated_chunk {};
        ChunkOverrideEntry pending_override {};
    };

    struct WaterPressureHead {
        int y = kWorldMinY;
        bool infinite = false;
    };

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
        std::array<std::bitset<kChunkSectionCount>, kLightingRegionSlots> changed_sky_sections {};
        std::array<std::uint8_t, kLightingRegionSlots> changed_sky_boundary_masks {};
        std::array<std::size_t, kLightingRegionSlots> block_light_difference_counts {};
        std::deque<LightNode> queue;
    };

    void mark_chunk_and_neighbors_dirty(const ChunkCoord& coord, const BlockCoord& local);
    void mark_neighbors_dirty(const ChunkCoord& coord);
    void mark_chunk_and_neighbors_lighting_dirty(const ChunkCoord& coord);
    void mark_sky_column_dirty(const ChunkCoord& coord, int local_x, int local_z);
    void load_chunk_immediate(const ChunkCoord& coord);
    void install_generated_chunk(Chunk&& chunk);
    void enqueue_generation_candidate(const ChunkCoord& coord, WorldStreamingStats* stats = nullptr);
    void enqueue_generation_area(const ChunkCoord& center, WorldStreamingStats& stats);
    void enqueue_generation_ring_transition(const ChunkCoord& previous_center, const ChunkCoord& next_center, WorldStreamingStats& stats);
    void prune_generation_queue(WorldStreamingStats& stats);
    void enqueue_lighting_update(const ChunkCoord& coord);
    void enqueue_fluid_cell(const BlockCoord& world_coord);
    void enqueue_adjacent_fluid_cells(const BlockCoord& world_coord);
    void enqueue_chunk_fluid_updates(const ChunkCoord& coord);
    void enqueue_chunk_fluid_boundary_updates(const ChunkCoord& coord, const ChunkCoord& boundary_direction);
    void invalidate_loaded_mesh_neighbors(const ChunkCoord& coord, bool defer_if_lighting_pending);
    void invalidate_loaded_mesh_neighbors_for_sections(
        const ChunkCoord& coord,
        const std::bitset<kChunkSectionCount>& dirty_sections,
        std::uint8_t boundary_mask);
    void invalidate_loaded_mesh_neighbors_for_chunk_load(const ChunkCoord& coord);
    void rebuild_pending_lighting_metadata();
    void flush_deferred_mesh_invalidations();
    void enqueue_mesh_rebuild(const ChunkCoord& coord, bool prioritize = false);
    void enqueue_dirty_chunks();
    void process_generation_queue(std::size_t budget, double max_ms, WorldWorkStats& stats);
    void process_fluid_queue(std::size_t budget, double max_ms, WorldWorkStats& stats);
    void process_lighting_queue(std::size_t budget, double max_ms, WorldWorkStats& stats);
    void process_mesh_queue(std::size_t budget, double max_ms, WorldWorkStats& stats);
    [[nodiscard]] auto collect_lighting_region(const ChunkCoord& anchor) const -> std::vector<ChunkCoord>;
    [[nodiscard]] auto initialize_lighting_job(LightingJob& job) -> bool;
    void rebuild_local_sky_light(LightingJob& job);
    void seed_local_block_lighting(LightingJob& job);
    void finalize_lighting_job(const LightingJob& job);
    [[nodiscard]] auto unload_far_chunks(const ChunkCoord& center) -> std::size_t;
    [[nodiscard]] auto rebuild_chunk_mesh(ChunkRecord& record) -> bool;
    void rebuild_chunk_mesh_cache(const ChunkRecord& record) const;
    void enqueue_gpu_upload(const ChunkCoord& coord);
    void enqueue_gpu_unload(const ChunkCoord& coord);
    void remove_unsupported_torches_around(int x, int y, int z);
    void refresh_chunk_emissive_cache(ChunkRecord& record);
    void update_chunk_emissive_cache(ChunkRecord& record, const BlockCoord& local_coord, BlockId previous_block, BlockId next_block);
    void sync_chunk_override_snapshot(const ChunkCoord& coord, const Chunk& chunk);
    void apply_chunk_override_to_record(ChunkRecord& record, const ChunkOverrideEntry& entry);
    [[nodiscard]] auto make_chunk_override_entry(
        const ChunkCoord& coord,
        const std::array<BlockId, kChunkVolume>& blocks,
        const std::array<WaterState, kChunkVolume>& water_state) const -> std::optional<ChunkOverrideEntry>;
    [[nodiscard]] auto materialize_chunk_override(
        const ChunkCoord& coord,
        const ChunkOverrideEntry& entry) const -> WorldChunkSnapshot;
    [[nodiscard]] auto find_sparse_override_cell(ChunkOverrideEntry& entry, std::size_t block_index)
        -> std::vector<ChunkOverrideCell>::iterator;
    [[nodiscard]] auto find_sparse_override_cell(const ChunkOverrideEntry& entry, std::size_t block_index) const
        -> std::vector<ChunkOverrideCell>::const_iterator;
    void set_chunk_override_cell(ChunkOverrideEntry& entry,
                                 std::size_t block_index,
                                 BlockId block,
                                 WaterState water_state,
                                 BlockId fallback_generated_block,
                                 WaterState fallback_generated_water_state,
                                 const Chunk* loaded_chunk);
    [[nodiscard]] auto normalize_water_state_for_generated(const BlockCoord& world_coord, WaterState water_state) const -> WaterState;
    [[nodiscard]] auto raw_water_state(int x, int y, int z) const -> WaterState;
    [[nodiscard]] auto can_water_flow_into_loaded(int x, int y, int z) const -> bool;
    [[nodiscard]] auto is_chunk_loaded_for_world(int x, int z) const noexcept -> bool;
    [[nodiscard]] auto is_infinite_water_source(const BlockCoord& world_coord, WaterState water_state) const -> bool;
    [[nodiscard]] auto set_water_state(int x, int y, int z, WaterState water_state) -> bool;
    [[nodiscard]] auto try_prepare_cell_for_water(int x, int y, int z) -> bool;
    [[nodiscard]] auto pressure_head_y_for(
        const BlockCoord& world_coord,
        WaterState water_state,
        std::unordered_map<BlockCoord, WaterPressureHead, BlockCoordHash>& pressure_head_cache,
        std::unordered_set<BlockCoord, BlockCoordHash>& pressure_head_missing_cache) -> std::optional<WaterPressureHead>;
    void update_chunk_override_after_cell_change(const ChunkCoord& coord,
                                                 const BlockCoord& local_coord,
                                                 BlockId previous_block,
                                                 BlockId next_block,
                                                 WaterState previous_water_state,
                                                 WaterState next_water_state);
    void apply_chunk_load_fluid_revalidation(const ChunkCoord& coord);
    [[nodiscard]] auto lighting_region_contains(const LightingJob& job, const ChunkCoord& coord) const noexcept -> bool;
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
    std::unordered_map<ChunkCoord, ChunkOverrideEntry, ChunkCoordHash> chunk_overrides_ {};
    std::deque<ChunkCoord> pending_generation_queue_ {};
    std::deque<BlockCoord> pending_fluid_queue_ {};
    std::unordered_set<ChunkCoord, ChunkCoordHash> pending_generation_set_ {};
    std::unordered_set<BlockCoord, BlockCoordHash> pending_fluid_set_ {};
    std::deque<ChunkCoord> pending_priority_mesh_queue_ {};
    std::deque<ChunkCoord> pending_mesh_queue_ {};
    std::deque<PendingLightingUpdate> pending_lighting_queue_ {};
    std::unordered_set<ChunkCoord, ChunkCoordHash> pending_mesh_set_ {};
    std::unordered_set<ChunkCoord, ChunkCoordHash> pending_priority_mesh_set_ {};
    std::unordered_set<ChunkCoord, ChunkCoordHash> deferred_mesh_invalidation_set_ {};
    std::unordered_set<ChunkCoord, ChunkCoordHash> pending_lighting_set_ {};
    std::unordered_set<ChunkCoord, ChunkCoordHash> pending_lighting_coverage_ {};
    std::unordered_set<ChunkCoord, ChunkCoordHash> active_lighting_coverage_ {};
    std::unordered_map<BlockCoord, WaterPressureHead, BlockCoordHash> fluid_pressure_head_cache_ {};
    std::unordered_set<BlockCoord, BlockCoordHash> fluid_pressure_head_missing_cache_ {};
    std::vector<BlockCoord> fluid_pressure_frontier_ {};
    std::vector<BlockCoord> fluid_pressure_visited_ {};
    std::unordered_set<BlockCoord, BlockCoordHash> fluid_pressure_seen_ {};
    std::deque<ChunkCoord> pending_gpu_uploads_ {};
    std::deque<ChunkCoord> pending_gpu_unloads_ {};
    std::unordered_set<ChunkCoord, ChunkCoordHash> pending_gpu_upload_set_ {};
    std::unordered_set<ChunkCoord, ChunkCoordHash> pending_gpu_unload_set_ {};
    std::optional<LightingJob> active_lighting_job_ {};
    std::unique_ptr<WorldGenerator::ChunkGenerationState> active_generation_job_ {};
    std::optional<SaveRestoreState> save_restore_state_ {};
    ChunkCoord stream_center_ {};
    bool has_stream_center_ = false;
    int active_stream_radius_ = 0;
};

} // namespace valcraft
