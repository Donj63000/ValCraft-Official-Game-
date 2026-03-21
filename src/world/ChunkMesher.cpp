#include "world/ChunkMesher.h"

#include "world/World.h"

#include <array>

namespace valcraft {

namespace {

enum class Face : int {
    PositiveX = 0,
    NegativeX = 1,
    PositiveY = 2,
    NegativeY = 3,
    PositiveZ = 4,
    NegativeZ = 5,
};

struct FaceDefinition {
    std::array<std::array<float, 3>, 4> positions;
    BlockCoord neighbor_offset;
    float shade;
};

constexpr std::array<FaceDefinition, 6> kFaceDefinitions {{
    {{{{1.0F, 0.0F, 0.0F}, {1.0F, 1.0F, 0.0F}, {1.0F, 1.0F, 1.0F}, {1.0F, 0.0F, 1.0F}}}, {1, 0, 0}, 0.85F},
    {{{{0.0F, 0.0F, 1.0F}, {0.0F, 1.0F, 1.0F}, {0.0F, 1.0F, 0.0F}, {0.0F, 0.0F, 0.0F}}}, {-1, 0, 0}, 0.85F},
    {{{{0.0F, 1.0F, 1.0F}, {1.0F, 1.0F, 1.0F}, {1.0F, 1.0F, 0.0F}, {0.0F, 1.0F, 0.0F}}}, {0, 1, 0}, 1.00F},
    {{{{0.0F, 0.0F, 0.0F}, {1.0F, 0.0F, 0.0F}, {1.0F, 0.0F, 1.0F}, {0.0F, 0.0F, 1.0F}}}, {0, -1, 0}, 0.65F},
    {{{{1.0F, 0.0F, 1.0F}, {1.0F, 1.0F, 1.0F}, {0.0F, 1.0F, 1.0F}, {0.0F, 0.0F, 1.0F}}}, {0, 0, 1}, 0.75F},
    {{{{0.0F, 0.0F, 0.0F}, {0.0F, 1.0F, 0.0F}, {1.0F, 1.0F, 0.0F}, {1.0F, 0.0F, 0.0F}}}, {0, 0, -1}, 0.75F},
}};

struct AtlasTile {
    int x = 0;
    int y = 0;
};

auto atlas_tile_for(BlockId block_id, Face face) -> AtlasTile {
    switch (static_cast<BlockType>(block_id)) {
    case BlockType::Grass:
        if (face == Face::PositiveY) {
            return {0, 0};
        }
        if (face == Face::NegativeY) {
            return {2, 0};
        }
        return {1, 0};
    case BlockType::Dirt:
        return {2, 0};
    case BlockType::Stone:
        return {3, 0};
    case BlockType::Sand:
        return {0, 1};
    case BlockType::Wood:
        if (face == Face::PositiveY || face == Face::NegativeY) {
            return {2, 1};
        }
        return {1, 1};
    case BlockType::Leaves:
        return {3, 1};
    case BlockType::Air:
    default:
        return {0, 0};
    }
}

void append_face(ChunkMeshData& mesh, BlockId block_id, Face face, int world_x, int world_y, int world_z) {
    constexpr float atlas_tiles_per_axis = 4.0F;
    constexpr float uv_step = 1.0F / atlas_tiles_per_axis;
    const auto& definition = kFaceDefinitions[static_cast<std::size_t>(face)];
    const auto tile = atlas_tile_for(block_id, face);
    const auto u0 = static_cast<float>(tile.x) * uv_step;
    const auto v0 = static_cast<float>(tile.y) * uv_step;
    const auto u1 = u0 + uv_step;
    const auto v1 = v0 + uv_step;

    const auto base_index = static_cast<std::uint32_t>(mesh.vertices.size());
    const std::array<std::array<float, 2>, 4> uvs {{
        {u1, v0},
        {u1, v1},
        {u0, v1},
        {u0, v0},
    }};

    for (std::size_t i = 0; i < definition.positions.size(); ++i) {
        const auto& vertex = definition.positions[i];
        mesh.vertices.push_back({
            static_cast<float>(world_x) + vertex[0],
            static_cast<float>(world_y) + vertex[1],
            static_cast<float>(world_z) + vertex[2],
            uvs[i][0],
            uvs[i][1],
            definition.shade,
        });
    }

    mesh.indices.insert(mesh.indices.end(), {
        base_index + 0U, base_index + 1U, base_index + 2U,
        base_index + 0U, base_index + 2U, base_index + 3U,
    });
    ++mesh.face_count;
}

} // namespace

auto ChunkMesher::build_mesh(const World& world, const ChunkCoord& coord) const -> ChunkMeshData {
    ChunkMeshData mesh {};
    const auto* chunk = world.find_chunk(coord);
    if (chunk == nullptr) {
        return mesh;
    }

    for (int y = 0; y < kChunkHeight; ++y) {
        for (int z = 0; z < kChunkSizeZ; ++z) {
            for (int x = 0; x < kChunkSizeX; ++x) {
                const auto block_id = chunk->get_local(x, y, z);
                if (!is_block_solid(block_id)) {
                    continue;
                }

                const auto world_coord = world.local_to_world(coord, {x, y, z});
                for (int face_index = 0; face_index < static_cast<int>(kFaceDefinitions.size()); ++face_index) {
                    const auto face = static_cast<Face>(face_index);
                    const auto& definition = kFaceDefinitions[static_cast<std::size_t>(face)];
                    const auto neighbor = BlockCoord {
                        world_coord.x + definition.neighbor_offset.x,
                        world_coord.y + definition.neighbor_offset.y,
                        world_coord.z + definition.neighbor_offset.z,
                    };
                    if (!is_block_solid(world.get_block(neighbor.x, neighbor.y, neighbor.z))) {
                        append_face(mesh, block_id, face, world_coord.x, world_coord.y, world_coord.z);
                    }
                }
            }
        }
    }

    return mesh;
}

} // namespace valcraft
