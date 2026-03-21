#pragma once

#include "world/Chunk.h"
#include "world/ChunkMesher.h"
#include "world/WorldGenerator.h"

#include <glm/vec3.hpp>

#include <cstdint>
#include <unordered_map>

namespace valcraft {

class World {
public:
    struct ChunkRecord {
        explicit ChunkRecord(ChunkCoord coord)
            : chunk(coord) {
        }

        Chunk chunk;
        ChunkMeshData mesh {};
        std::uint64_t mesh_revision = 0;
    };

    explicit World(int seed = 1337, int stream_radius = kDefaultStreamRadius);

    [[nodiscard]] auto get_block(int x, int y, int z) const -> BlockId;
    void set_block(int x, int y, int z, BlockId block_id);

    [[nodiscard]] auto world_to_chunk(int x, int z) const noexcept -> ChunkCoord;
    [[nodiscard]] auto world_to_local(int x, int y, int z) const noexcept -> BlockCoord;
    [[nodiscard]] auto local_to_world(const ChunkCoord& chunk_coord, const BlockCoord& local) const noexcept -> BlockCoord;
    [[nodiscard]] auto raycast(const glm::vec3& origin, const glm::vec3& direction, float max_distance) const -> RaycastHit;

    void ensure_chunk_loaded(const ChunkCoord& coord);
    void update_streaming(const glm::vec3& player_position);
    void rebuild_dirty_meshes();

    [[nodiscard]] auto find_chunk(const ChunkCoord& coord) -> Chunk*;
    [[nodiscard]] auto find_chunk(const ChunkCoord& coord) const -> const Chunk*;
    [[nodiscard]] auto mesh_for(const ChunkCoord& coord) const -> const ChunkMeshData*;
    [[nodiscard]] auto mesh_revision(const ChunkCoord& coord) const -> std::uint64_t;
    [[nodiscard]] auto chunk_records() const noexcept -> const std::unordered_map<ChunkCoord, ChunkRecord, ChunkCoordHash>&;

    [[nodiscard]] auto seed() const noexcept -> int;
    [[nodiscard]] auto stream_radius() const noexcept -> int;
    [[nodiscard]] auto surface_height(int world_x, int world_z) -> int;

    [[nodiscard]] static auto floor_div(int value, int divisor) noexcept -> int;
    [[nodiscard]] static auto positive_mod(int value, int divisor) noexcept -> int;

private:
    void mark_chunk_and_neighbors_dirty(const ChunkCoord& coord, const BlockCoord& local);
    void mark_neighbors_dirty(const ChunkCoord& coord);
    void unload_far_chunks(const ChunkCoord& center);
    void rebuild_chunk_mesh(ChunkRecord& record);

    int stream_radius_ = kDefaultStreamRadius;
    WorldGenerator generator_;
    ChunkMesher mesher_;
    std::unordered_map<ChunkCoord, ChunkRecord, ChunkCoordHash> chunks_ {};
};

} // namespace valcraft
