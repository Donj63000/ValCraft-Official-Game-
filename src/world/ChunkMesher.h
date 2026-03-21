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
    float shade = 1.0F;
};

struct ChunkMeshData {
    std::vector<ChunkVertex> vertices;
    std::vector<std::uint32_t> indices;
    std::size_t face_count = 0;

    [[nodiscard]] auto empty() const noexcept -> bool {
        return indices.empty();
    }
};

class ChunkMesher {
public:
    [[nodiscard]] auto build_mesh(const World& world, const ChunkCoord& coord) const -> ChunkMeshData;
};

} // namespace valcraft
