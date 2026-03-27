#pragma once

#include "world/Block.h"

#include <cstdint>
#include <vector>

namespace valcraft {

class World;

struct ChunkVertex {
    float x = 0.0F;
    float y = 0.0F;
    float z = 0.0F;
    float u = 0.0F;
    float v = 0.0F;
    float nx = 0.0F;
    float ny = 1.0F;
    float nz = 0.0F;
    float face_shade = 1.0F;
    float ao = 1.0F;
    float sky_light = 1.0F;
    float block_light = 0.0F;
    float material_class = 0.0F;
};

struct ChunkMeshData {
    std::vector<ChunkVertex> vertices;
    std::vector<std::uint32_t> indices;
    std::vector<ChunkVertex> water_vertices;
    std::vector<std::uint32_t> water_indices;
    std::size_t face_count = 0;
    std::size_t water_face_count = 0;

    [[nodiscard]] auto empty() const noexcept -> bool {
        return indices.empty() && water_indices.empty();
    }

    [[nodiscard]] auto total_vertex_count() const noexcept -> std::size_t {
        return vertices.size() + water_vertices.size();
    }

    [[nodiscard]] auto total_index_count() const noexcept -> std::size_t {
        return indices.size() + water_indices.size();
    }
};

class ChunkMesher {
public:
    [[nodiscard]] auto build_mesh(const World& world,
                                  const ChunkCoord& coord,
                                  std::size_t vertex_reserve_hint = 0,
                                  std::size_t index_reserve_hint = 0) const -> ChunkMeshData;
    [[nodiscard]] auto build_mesh_range(const World& world,
                                        const ChunkCoord& coord,
                                        int min_y,
                                        int max_y,
                                        std::size_t vertex_reserve_hint = 0,
                                        std::size_t index_reserve_hint = 0) const -> ChunkMeshData;
};

} // namespace valcraft
