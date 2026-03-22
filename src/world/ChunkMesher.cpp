#include "world/ChunkMesher.h"

#include "world/World.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <utility>

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

struct VertexPattern {
    std::array<float, 3> position;
    BlockCoord side1;
    BlockCoord side2;
};

struct FaceDefinition {
    std::array<VertexPattern, 4> vertices;
    BlockCoord neighbor_offset;
    std::array<float, 3> normal;
    float face_shade;
};

constexpr auto kCachedSpanX = kChunkSizeX + 2;
constexpr auto kCachedSpanZ = kChunkSizeZ + 2;
constexpr auto kCachedNeighborhoodVolume = static_cast<std::size_t>(kCachedSpanX * kChunkHeight * kCachedSpanZ);

struct Neighborhood {
    std::array<const Chunk*, 9> chunks {};
    std::array<BlockId, kCachedNeighborhoodVolume> blocks {};
    std::array<std::uint8_t, kCachedNeighborhoodVolume> sky_light {};
    std::array<std::uint8_t, kCachedNeighborhoodVolume> block_light {};

    [[nodiscard]] auto sample_chunk(int x, int z, int& local_x, int& local_z) const -> const Chunk* {
        const auto chunk_x = x < 0 ? -1 : (x >= kChunkSizeX ? 1 : 0);
        const auto chunk_z = z < 0 ? -1 : (z >= kChunkSizeZ ? 1 : 0);
        local_x = x < 0 ? x + kChunkSizeX : (x >= kChunkSizeX ? x - kChunkSizeX : x);
        local_z = z < 0 ? z + kChunkSizeZ : (z >= kChunkSizeZ ? z - kChunkSizeZ : z);
        const auto index = static_cast<std::size_t>((chunk_z + 1) * 3 + (chunk_x + 1));
        return chunks[index];
    }

    [[nodiscard]] static auto cache_index(int x, int y, int z) noexcept -> std::size_t {
        return static_cast<std::size_t>(((y * kCachedSpanZ) + (z + 1)) * kCachedSpanX + (x + 1));
    }

    void populate() {
        for (int y = kWorldMinY; y <= kWorldMaxY; ++y) {
            for (int z = -1; z <= kChunkSizeZ; ++z) {
                for (int x = -1; x <= kChunkSizeX; ++x) {
                    auto local_x = x;
                    auto local_z = z;
                    const auto* chunk = sample_chunk(x, z, local_x, local_z);
                    const auto index = cache_index(x, y, z);
                    if (chunk == nullptr) {
                        blocks[index] = to_block_id(BlockType::Air);
                        sky_light[index] = 15;
                        block_light[index] = 0;
                        continue;
                    }

                    blocks[index] = chunk->get_local(local_x, y, local_z);
                    sky_light[index] = chunk->get_sky_light_local(local_x, y, local_z);
                    block_light[index] = chunk->get_block_light_local(local_x, y, local_z);
                }
            }
        }
    }

    [[nodiscard]] auto block_at(int x, int y, int z) const -> BlockId {
        if (!is_world_y_valid(y)) {
            return to_block_id(BlockType::Air);
        }
        return blocks[cache_index(x, y, z)];
    }

    [[nodiscard]] auto sky_light_at(int x, int y, int z) const -> std::uint8_t {
        if (!is_world_y_valid(y)) {
            return 0;
        }
        return sky_light[cache_index(x, y, z)];
    }

    [[nodiscard]] auto block_light_at(int x, int y, int z) const -> std::uint8_t {
        if (!is_world_y_valid(y)) {
            return 0;
        }
        return block_light[cache_index(x, y, z)];
    }
};

constexpr auto make_vertex(float x, float y, float z, BlockCoord side1, BlockCoord side2) -> VertexPattern {
    return {{{x, y, z}}, side1, side2};
}

constexpr auto make_face_definition(
    std::array<VertexPattern, 4> vertices,
    BlockCoord neighbor_offset,
    std::array<float, 3> normal,
    float face_shade) -> FaceDefinition {
    return {vertices, neighbor_offset, normal, face_shade};
}

constexpr std::array<FaceDefinition, 6> kFaceDefinitions {{
    make_face_definition(
        {{
            make_vertex(1.0F, 0.0F, 0.0F, {0, -1, 0}, {0, 0, -1}),
            make_vertex(1.0F, 1.0F, 0.0F, {0, 1, 0}, {0, 0, -1}),
            make_vertex(1.0F, 1.0F, 1.0F, {0, 1, 0}, {0, 0, 1}),
            make_vertex(1.0F, 0.0F, 1.0F, {0, -1, 0}, {0, 0, 1}),
        }},
        {1, 0, 0},
        {1.0F, 0.0F, 0.0F},
        0.85F),
    make_face_definition(
        {{
            make_vertex(0.0F, 0.0F, 1.0F, {0, -1, 0}, {0, 0, 1}),
            make_vertex(0.0F, 1.0F, 1.0F, {0, 1, 0}, {0, 0, 1}),
            make_vertex(0.0F, 1.0F, 0.0F, {0, 1, 0}, {0, 0, -1}),
            make_vertex(0.0F, 0.0F, 0.0F, {0, -1, 0}, {0, 0, -1}),
        }},
        {-1, 0, 0},
        {-1.0F, 0.0F, 0.0F},
        0.85F),
    make_face_definition(
        {{
            make_vertex(0.0F, 1.0F, 1.0F, {-1, 0, 0}, {0, 0, 1}),
            make_vertex(1.0F, 1.0F, 1.0F, {1, 0, 0}, {0, 0, 1}),
            make_vertex(1.0F, 1.0F, 0.0F, {1, 0, 0}, {0, 0, -1}),
            make_vertex(0.0F, 1.0F, 0.0F, {-1, 0, 0}, {0, 0, -1}),
        }},
        {0, 1, 0},
        {0.0F, 1.0F, 0.0F},
        1.00F),
    make_face_definition(
        {{
            make_vertex(0.0F, 0.0F, 0.0F, {-1, 0, 0}, {0, 0, -1}),
            make_vertex(1.0F, 0.0F, 0.0F, {1, 0, 0}, {0, 0, -1}),
            make_vertex(1.0F, 0.0F, 1.0F, {1, 0, 0}, {0, 0, 1}),
            make_vertex(0.0F, 0.0F, 1.0F, {-1, 0, 0}, {0, 0, 1}),
        }},
        {0, -1, 0},
        {0.0F, -1.0F, 0.0F},
        0.65F),
    make_face_definition(
        {{
            make_vertex(1.0F, 0.0F, 1.0F, {1, 0, 0}, {0, -1, 0}),
            make_vertex(1.0F, 1.0F, 1.0F, {1, 0, 0}, {0, 1, 0}),
            make_vertex(0.0F, 1.0F, 1.0F, {-1, 0, 0}, {0, 1, 0}),
            make_vertex(0.0F, 0.0F, 1.0F, {-1, 0, 0}, {0, -1, 0}),
        }},
        {0, 0, 1},
        {0.0F, 0.0F, 1.0F},
        0.75F),
    make_face_definition(
        {{
            make_vertex(0.0F, 0.0F, 0.0F, {-1, 0, 0}, {0, -1, 0}),
            make_vertex(0.0F, 1.0F, 0.0F, {-1, 0, 0}, {0, 1, 0}),
            make_vertex(1.0F, 1.0F, 0.0F, {1, 0, 0}, {0, 1, 0}),
            make_vertex(1.0F, 0.0F, 0.0F, {1, 0, 0}, {0, -1, 0}),
        }},
        {0, 0, -1},
        {0.0F, 0.0F, -1.0F},
        0.75F),
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
    case BlockType::Torch:
        return {0, 2};
    case BlockType::Air:
    default:
        return {0, 0};
    }
}

auto build_neighborhood(const World& world, const ChunkCoord& coord) -> Neighborhood {
    Neighborhood neighborhood {};
    // AO and vertex lighting sample the full 3x3 chunk neighborhood around the mesh,
    // so neighbor load/unload events must invalidate already-built border meshes.
    for (int dz = -1; dz <= 1; ++dz) {
        for (int dx = -1; dx <= 1; ++dx) {
            const auto index = static_cast<std::size_t>((dz + 1) * 3 + (dx + 1));
            neighborhood.chunks[index] = world.find_chunk({coord.x + dx, coord.z + dz});
        }
    }
    neighborhood.populate();
    return neighborhood;
}

auto is_occluder(const Neighborhood& neighborhood, const BlockCoord& local_coord) -> bool {
    return is_block_opaque(neighborhood.block_at(local_coord.x, local_coord.y, local_coord.z));
}

auto add_offset(const BlockCoord& lhs, const BlockCoord& rhs) -> BlockCoord {
    return {
        lhs.x + rhs.x,
        lhs.y + rhs.y,
        lhs.z + rhs.z,
    };
}

auto compute_vertex_ao(const Neighborhood& neighborhood, const BlockCoord& local_coord, const VertexPattern& vertex) -> int {
    const auto side_a_coord = add_offset(local_coord, vertex.side1);
    const auto side_b_coord = add_offset(local_coord, vertex.side2);
    const auto corner_coord = add_offset(side_a_coord, vertex.side2);
    const auto side_a = is_occluder(neighborhood, side_a_coord);
    const auto side_b = is_occluder(neighborhood, side_b_coord);
    const auto corner = is_occluder(neighborhood, corner_coord);

    if (side_a && side_b) {
        return 0;
    }
    return 3 - static_cast<int>(side_a) - static_cast<int>(side_b) - static_cast<int>(corner);
}

auto ao_factor(int raw_ao) -> float {
    return 0.55F + static_cast<float>(raw_ao) * 0.15F;
}

auto sample_vertex_light(const Neighborhood& neighborhood,
                         const BlockCoord& local_coord,
                         const FaceDefinition& definition,
                         const VertexPattern& vertex) -> std::pair<float, float> {
    const auto corner_offset = add_offset(vertex.side1, vertex.side2);
    const std::array<BlockCoord, 4> sample_offsets {{
        definition.neighbor_offset,
        add_offset(definition.neighbor_offset, vertex.side1),
        add_offset(definition.neighbor_offset, vertex.side2),
        add_offset(definition.neighbor_offset, corner_offset),
    }};

    std::uint8_t sky_light = 0;
    std::uint8_t block_light = 0;
    for (const auto& offset : sample_offsets) {
        const auto sample_coord = add_offset(local_coord, offset);
        if (is_block_opaque(neighborhood.block_at(sample_coord.x, sample_coord.y, sample_coord.z))) {
            continue;
        }
        sky_light = std::max(sky_light, neighborhood.sky_light_at(sample_coord.x, sample_coord.y, sample_coord.z));
        block_light = std::max(block_light, neighborhood.block_light_at(sample_coord.x, sample_coord.y, sample_coord.z));
    }

    return {
        static_cast<float>(sky_light) / 15.0F,
        static_cast<float>(block_light) / 15.0F,
    };
}

void append_indices(ChunkMeshData& mesh, std::uint32_t base_index, bool flip_diagonal) {
    if (flip_diagonal) {
        mesh.indices.insert(mesh.indices.end(), {
            base_index + 0U, base_index + 1U, base_index + 3U,
            base_index + 1U, base_index + 2U, base_index + 3U,
        });
    } else {
        mesh.indices.insert(mesh.indices.end(), {
            base_index + 0U, base_index + 1U, base_index + 2U,
            base_index + 0U, base_index + 2U, base_index + 3U,
        });
    }
    ++mesh.face_count;
}

void append_face_geometry(
    ChunkMeshData& mesh,
    const FaceDefinition& definition,
    const AtlasTile& tile,
    const std::array<std::array<float, 3>, 4>& positions,
    const std::array<float, 4>& ao_values,
    const std::array<float, 4>& sky_values,
    const std::array<float, 4>& block_values) {
    constexpr float atlas_tiles_per_axis = 4.0F;
    constexpr float uv_step = 1.0F / atlas_tiles_per_axis;
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

    for (std::size_t i = 0; i < positions.size(); ++i) {
        mesh.vertices.push_back({
            positions[i][0],
            positions[i][1],
            positions[i][2],
            uvs[i][0],
            uvs[i][1],
            definition.normal[0],
            definition.normal[1],
            definition.normal[2],
            definition.face_shade,
            ao_values[i],
            sky_values[i],
            block_values[i],
        });
    }

    const auto first_diagonal = ao_values[0] + ao_values[2];
    const auto second_diagonal = ao_values[1] + ao_values[3];
    append_indices(mesh, base_index, first_diagonal < second_diagonal);
}

void append_cube_face(ChunkMeshData& mesh,
                      const Neighborhood& neighborhood,
                      BlockId block_id,
                      Face face,
                      const BlockCoord& local_coord,
                      int chunk_world_x,
                      int chunk_world_z) {
    const auto& definition = kFaceDefinitions[static_cast<std::size_t>(face)];
    const auto tile = atlas_tile_for(block_id, face);

    std::array<std::array<float, 3>, 4> positions {};
    std::array<float, 4> ao_values {};
    std::array<float, 4> sky_values {};
    std::array<float, 4> block_values {};
    std::array<int, 4> raw_ao_values {};

    for (std::size_t i = 0; i < definition.vertices.size(); ++i) {
        const auto& vertex = definition.vertices[i];
        positions[i] = {
            static_cast<float>(chunk_world_x + local_coord.x) + vertex.position[0],
            static_cast<float>(local_coord.y) + vertex.position[1],
            static_cast<float>(chunk_world_z + local_coord.z) + vertex.position[2],
        };

        raw_ao_values[i] = compute_vertex_ao(neighborhood, local_coord, vertex);
        ao_values[i] = ao_factor(raw_ao_values[i]);
        const auto [sky_light, block_light] = sample_vertex_light(neighborhood, local_coord, definition, vertex);
        sky_values[i] = sky_light;
        block_values[i] = block_light;
    }

    append_face_geometry(mesh, definition, tile, positions, ao_values, sky_values, block_values);
}

void append_torch_mesh(ChunkMeshData& mesh,
                       const Neighborhood& neighborhood,
                       const BlockCoord& local_coord,
                       int chunk_world_x,
                       int chunk_world_z) {
    constexpr float min_x = 0.43F;
    constexpr float max_x = 0.57F;
    constexpr float min_z = 0.43F;
    constexpr float max_z = 0.57F;
    constexpr float min_y = 0.0F;
    constexpr float max_y = 0.72F;
    constexpr float torch_ao = 1.0F;

    const auto torch_light = std::max(
        neighborhood.block_light_at(local_coord.x, local_coord.y, local_coord.z),
        static_cast<std::uint8_t>(14));
    const auto sky_light = neighborhood.sky_light_at(local_coord.x, local_coord.y, local_coord.z);
    const auto normalized_block = static_cast<float>(torch_light) / 15.0F;
    const auto normalized_sky = static_cast<float>(sky_light) / 15.0F;
    const std::array<float, 4> ao_values {{torch_ao, torch_ao, torch_ao, torch_ao}};
    const std::array<float, 4> sky_values {{normalized_sky, normalized_sky, normalized_sky, normalized_sky}};
    const std::array<float, 4> block_values {{normalized_block, normalized_block, normalized_block, normalized_block}};

    auto append_prism_face = [&](Face face, const std::array<std::array<float, 3>, 4>& local_positions) {
        std::array<std::array<float, 3>, 4> positions {};
        for (std::size_t i = 0; i < local_positions.size(); ++i) {
            positions[i] = {
                static_cast<float>(chunk_world_x + local_coord.x) + local_positions[i][0],
                static_cast<float>(local_coord.y) + local_positions[i][1],
                static_cast<float>(chunk_world_z + local_coord.z) + local_positions[i][2],
            };
        }

        append_face_geometry(
            mesh,
            kFaceDefinitions[static_cast<std::size_t>(face)],
            atlas_tile_for(to_block_id(BlockType::Torch), face),
            positions,
            ao_values,
            sky_values,
            block_values);
    };

    append_prism_face(Face::PositiveX, {{{max_x, min_y, min_z}, {max_x, max_y, min_z}, {max_x, max_y, max_z}, {max_x, min_y, max_z}}});
    append_prism_face(Face::NegativeX, {{{min_x, min_y, max_z}, {min_x, max_y, max_z}, {min_x, max_y, min_z}, {min_x, min_y, min_z}}});
    append_prism_face(Face::PositiveY, {{{min_x, max_y, max_z}, {max_x, max_y, max_z}, {max_x, max_y, min_z}, {min_x, max_y, min_z}}});
    append_prism_face(Face::NegativeY, {{{min_x, min_y, min_z}, {max_x, min_y, min_z}, {max_x, min_y, max_z}, {min_x, min_y, max_z}}});
    append_prism_face(Face::PositiveZ, {{{max_x, min_y, max_z}, {max_x, max_y, max_z}, {min_x, max_y, max_z}, {min_x, min_y, max_z}}});
    append_prism_face(Face::NegativeZ, {{{min_x, min_y, min_z}, {min_x, max_y, min_z}, {max_x, max_y, min_z}, {max_x, min_y, min_z}}});
}

} // namespace

auto ChunkMesher::build_mesh(const World& world,
                             const ChunkCoord& coord,
                             std::size_t vertex_reserve_hint,
                             std::size_t index_reserve_hint) const -> ChunkMeshData {
    ChunkMeshData mesh {};
    const auto* chunk = world.find_chunk(coord);
    if (chunk == nullptr) {
        return mesh;
    }

    mesh.vertices.reserve(vertex_reserve_hint > 0 ? vertex_reserve_hint : 2048U);
    mesh.indices.reserve(index_reserve_hint > 0 ? index_reserve_hint : 3072U);

    const auto neighborhood = build_neighborhood(world, coord);
    const auto chunk_world_x = coord.x * kChunkSizeX;
    const auto chunk_world_z = coord.z * kChunkSizeZ;

    for (int y = 0; y < kChunkHeight; ++y) {
        for (int z = 0; z < kChunkSizeZ; ++z) {
            for (int x = 0; x < kChunkSizeX; ++x) {
                const auto block_id = chunk->get_local(x, y, z);
                if (!is_block_solid(block_id)) {
                    continue;
                }

                const BlockCoord local_coord {x, y, z};
                if (block_mesh_type(block_id) == BlockMeshType::Torch) {
                    append_torch_mesh(mesh, neighborhood, local_coord, chunk_world_x, chunk_world_z);
                    continue;
                }

                for (int face_index = 0; face_index < static_cast<int>(kFaceDefinitions.size()); ++face_index) {
                    const auto face = static_cast<Face>(face_index);
                    const auto& definition = kFaceDefinitions[static_cast<std::size_t>(face)];
                    const auto neighbor = BlockCoord {
                        local_coord.x + definition.neighbor_offset.x,
                        local_coord.y + definition.neighbor_offset.y,
                        local_coord.z + definition.neighbor_offset.z,
                    };
                    if (!is_block_opaque(neighborhood.block_at(neighbor.x, neighbor.y, neighbor.z))) {
                        append_cube_face(mesh, neighborhood, block_id, face, local_coord, chunk_world_x, chunk_world_z);
                    }
                }
            }
        }
    }

    return mesh;
}

} // namespace valcraft
