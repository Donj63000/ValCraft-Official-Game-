#include "world/ChunkMesher.h"

#include "world/BlockVisuals.h"
#include "world/World.h"

#include <algorithm>
#include <array>
#include <cmath>
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
constexpr float kWaterSurfaceHeight = 15.0F / 16.0F;
constexpr float kWaterSurfaceRepeatBlocks = 8.0F;
constexpr int kWaterSurfaceSubdivisions = 4;
constexpr int kWaterSideVerticalSubdivisions = 2;
constexpr int kWaterSideHorizontalSubdivisions = 4;

using Float2 = std::array<float, 2>;
using Float3 = std::array<float, 3>;

auto chunk_linear_index(int local_x, int local_y, int local_z) noexcept -> std::size_t {
    return static_cast<std::size_t>((local_y * kChunkSizeZ + local_z) * kChunkSizeX + local_x);
}

struct Neighborhood {
    std::array<const Chunk*, 9> chunks {};
    std::array<BlockId, kCachedNeighborhoodVolume> blocks {};
    std::array<std::uint8_t, kCachedNeighborhoodVolume> sky_light {};
    std::array<std::uint8_t, kCachedNeighborhoodVolume> block_light {};
    int min_cached_y = kWorldMinY;
    int max_cached_y = kWorldMaxY;

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

    void populate(int min_y, int max_y) {
        min_cached_y = std::clamp(min_y, kWorldMinY, kWorldMaxY);
        max_cached_y = std::clamp(max_y, kWorldMinY, kWorldMaxY);
        if (max_cached_y < min_cached_y) {
            min_cached_y = kWorldMinY;
            max_cached_y = kWorldMinY - 1;
            return;
        }

        for (int y = min_cached_y; y <= max_cached_y; ++y) {
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

                    const auto local_index = chunk_linear_index(local_x, y, local_z);
                    const auto& chunk_blocks = chunk->blocks();
                    const auto& chunk_sky_light = chunk->sky_light();
                    const auto& chunk_block_light = chunk->block_light();
                    blocks[index] = chunk_blocks[local_index];
                    sky_light[index] = chunk_sky_light[local_index];
                    block_light[index] = chunk_block_light[local_index];
                }
            }
        }
    }

    [[nodiscard]] auto block_at(int x, int y, int z) const -> BlockId {
        if (!is_world_y_valid(y) || y < min_cached_y || y > max_cached_y) {
            return to_block_id(BlockType::Air);
        }
        return blocks[cache_index(x, y, z)];
    }

    [[nodiscard]] auto sky_light_at(int x, int y, int z) const -> std::uint8_t {
        if (!is_world_y_valid(y)) {
            return 0;
        }
        if (y < min_cached_y || y > max_cached_y) {
            return 15;
        }
        return sky_light[cache_index(x, y, z)];
    }

    [[nodiscard]] auto block_light_at(int x, int y, int z) const -> std::uint8_t {
        if (!is_world_y_valid(y) || y < min_cached_y || y > max_cached_y) {
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

auto to_visual_face(Face face) noexcept -> BlockVisualFace {
    switch (face) {
    case Face::PositiveX:
        return BlockVisualFace::PositiveX;
    case Face::NegativeX:
        return BlockVisualFace::NegativeX;
    case Face::PositiveY:
        return BlockVisualFace::PositiveY;
    case Face::NegativeY:
        return BlockVisualFace::NegativeY;
    case Face::PositiveZ:
        return BlockVisualFace::PositiveZ;
    case Face::NegativeZ:
    default:
        return BlockVisualFace::NegativeZ;
    }
}

auto build_neighborhood(const World& world, const ChunkCoord& coord, int min_y, int max_y) -> Neighborhood {
    Neighborhood neighborhood {};
    // AO and vertex lighting sample the full 3x3 chunk neighborhood around the mesh,
    // so neighbor load/unload events must invalidate already-built border meshes.
    for (int dz = -1; dz <= 1; ++dz) {
        for (int dx = -1; dx <= 1; ++dx) {
            const auto index = static_cast<std::size_t>((dz + 1) * 3 + (dx + 1));
            neighborhood.chunks[index] = world.find_chunk({coord.x + dx, coord.z + dz});
        }
    }
    neighborhood.populate(min_y, max_y);
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

auto lerp_float(float start, float end, float t) noexcept -> float {
    return start + (end - start) * t;
}

auto lerp_vec2(const Float2& start, const Float2& end, float t) noexcept -> Float2 {
    return {
        lerp_float(start[0], end[0], t),
        lerp_float(start[1], end[1], t),
    };
}

auto lerp_vec3(const Float3& start, const Float3& end, float t) noexcept -> Float3 {
    return {
        lerp_float(start[0], end[0], t),
        lerp_float(start[1], end[1], t),
        lerp_float(start[2], end[2], t),
    };
}

auto bilerp_float(const std::array<float, 4>& corners, float u, float v) noexcept -> float {
    const auto top = lerp_float(corners[0], corners[1], u);
    const auto bottom = lerp_float(corners[3], corners[2], u);
    return lerp_float(top, bottom, v);
}

auto bilerp_vec2(const std::array<Float2, 4>& corners, float u, float v) noexcept -> Float2 {
    const auto top = lerp_vec2(corners[0], corners[1], u);
    const auto bottom = lerp_vec2(corners[3], corners[2], u);
    return lerp_vec2(top, bottom, v);
}

auto bilerp_vec3(const std::array<Float3, 4>& corners, float u, float v) noexcept -> Float3 {
    const auto top = lerp_vec3(corners[0], corners[1], u);
    const auto bottom = lerp_vec3(corners[3], corners[2], u);
    return lerp_vec3(top, bottom, v);
}

struct WaterFaceSubdivisions {
    int u = 1;
    int v = 1;
};

auto water_face_subdivisions(Face face) noexcept -> WaterFaceSubdivisions {
    switch (face) {
    case Face::PositiveY:
        return {kWaterSurfaceSubdivisions, kWaterSurfaceSubdivisions};
    case Face::NegativeY:
        return {1, 1};
    case Face::PositiveX:
    case Face::NegativeX:
    case Face::PositiveZ:
    case Face::NegativeZ:
    default:
        return {kWaterSideVerticalSubdivisions, kWaterSideHorizontalSubdivisions};
    }
}

auto wrap_positive(float value, float period) noexcept -> float {
    const auto wrapped = std::fmod(value, period);
    return wrapped < 0.0F ? wrapped + period : wrapped;
}

auto repeating_water_surface_uv(const BlockAtlasTile& tile,
                                int block_world_x,
                                int block_world_z,
                                float local_x,
                                float local_z) -> std::array<float, 2> {
    const auto uv_step = 1.0F / kBlockAtlasTilesPerAxis;
    const auto u0 = static_cast<float>(tile.x) * uv_step;
    const auto v0 = static_cast<float>(tile.y) * uv_step;

    // Je garde exactement un huitieme de tuile par bloc pour eviter
    // les sauts de 7/8 vers 0 au passage de la frontiere de repetition.
    const auto block_u = wrap_positive(static_cast<float>(block_world_x), kWaterSurfaceRepeatBlocks) / kWaterSurfaceRepeatBlocks;
    const auto block_v = wrap_positive(static_cast<float>(block_world_z), kWaterSurfaceRepeatBlocks) / kWaterSurfaceRepeatBlocks;
    const auto surface_u = std::min(block_u + local_x / kWaterSurfaceRepeatBlocks, 1.0F);
    const auto surface_v = std::min(block_v + local_z / kWaterSurfaceRepeatBlocks, 1.0F);

    return {
        u0 + surface_u * uv_step,
        v0 + surface_v * uv_step,
    };
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

void append_quad_indices(std::vector<std::uint32_t>& indices,
                         std::uint32_t index0,
                         std::uint32_t index1,
                         std::uint32_t index2,
                         std::uint32_t index3,
                         bool flip_diagonal) {
    if (flip_diagonal) {
        indices.insert(indices.end(), {
            index0, index1, index3,
            index1, index2, index3,
        });
    } else {
        indices.insert(indices.end(), {
            index0, index1, index2,
            index0, index2, index3,
        });
    }
}

void append_indices(ChunkMeshData& mesh, std::uint32_t base_index, bool flip_diagonal) {
    append_quad_indices(mesh.indices, base_index + 0U, base_index + 1U, base_index + 2U, base_index + 3U, flip_diagonal);
    ++mesh.face_count;
}

void append_face_geometry(
    ChunkMeshData& mesh,
    const FaceDefinition& definition,
    const BlockAtlasTile& tile,
    const std::array<std::array<float, 3>, 4>& positions,
    const std::array<float, 4>& ao_values,
    const std::array<float, 4>& sky_values,
    const std::array<float, 4>& block_values,
    float material_class) {
    const auto uv_step = 1.0F / kBlockAtlasTilesPerAxis;
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
            material_class,
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
    const auto tile = block_atlas_tile(block_id, to_visual_face(face));
    const auto material_class = block_visual_material_value(block_id);

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

    append_face_geometry(mesh, definition, tile, positions, ao_values, sky_values, block_values, material_class);
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
    const auto material_class = block_visual_material_value(to_block_id(BlockType::Torch));

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
            block_atlas_tile(to_block_id(BlockType::Torch), to_visual_face(face)),
            positions,
            ao_values,
            sky_values,
            block_values,
            material_class);
    };

    append_prism_face(Face::PositiveX, {{{max_x, min_y, min_z}, {max_x, max_y, min_z}, {max_x, max_y, max_z}, {max_x, min_y, max_z}}});
    append_prism_face(Face::NegativeX, {{{min_x, min_y, max_z}, {min_x, max_y, max_z}, {min_x, max_y, min_z}, {min_x, min_y, min_z}}});
    append_prism_face(Face::PositiveY, {{{min_x, max_y, max_z}, {max_x, max_y, max_z}, {max_x, max_y, min_z}, {min_x, max_y, min_z}}});
    append_prism_face(Face::NegativeY, {{{min_x, min_y, min_z}, {max_x, min_y, min_z}, {max_x, min_y, max_z}, {min_x, min_y, max_z}}});
    append_prism_face(Face::PositiveZ, {{{max_x, min_y, max_z}, {max_x, max_y, max_z}, {min_x, max_y, max_z}, {min_x, min_y, max_z}}});
    append_prism_face(Face::NegativeZ, {{{min_x, min_y, min_z}, {min_x, max_y, min_z}, {max_x, max_y, min_z}, {max_x, min_y, min_z}}});
}

void append_cross_quad(ChunkMeshData& mesh,
                       const BlockAtlasTile& tile,
                       const std::array<std::array<float, 3>, 4>& positions,
                       const std::array<float, 3>& normal,
                       float face_shade,
                       float ao,
                       float sky_light,
                       float block_light,
                       float material_class) {
    const auto uv_step = 1.0F / kBlockAtlasTilesPerAxis;
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
            normal[0],
            normal[1],
            normal[2],
            face_shade,
            ao,
            sky_light,
            block_light,
            material_class,
        });
    }

    mesh.indices.insert(mesh.indices.end(), {
        base_index + 0U, base_index + 1U, base_index + 2U,
        base_index + 0U, base_index + 2U, base_index + 3U,
    });
    ++mesh.face_count;
}

void append_cross_mesh(ChunkMeshData& mesh,
                       const Neighborhood& neighborhood,
                       BlockId block_id,
                       const BlockCoord& local_coord,
                       int chunk_world_x,
                       int chunk_world_z) {
    const auto tile = block_atlas_tile(block_id, BlockVisualFace::Cross);
    const auto material_class = block_visual_material_value(block_id);
    const auto world_x = static_cast<float>(chunk_world_x + local_coord.x);
    const auto world_y = static_cast<float>(local_coord.y);
    const auto world_z = static_cast<float>(chunk_world_z + local_coord.z);
    const auto sky_light = static_cast<float>(std::max(
        neighborhood.sky_light_at(local_coord.x, local_coord.y, local_coord.z),
        neighborhood.sky_light_at(local_coord.x, local_coord.y + 1, local_coord.z))) / 15.0F;
    const auto block_light = static_cast<float>(std::max(
        neighborhood.block_light_at(local_coord.x, local_coord.y, local_coord.z),
        neighborhood.block_light_at(local_coord.x, local_coord.y + 1, local_coord.z))) / 15.0F;

    const std::array<std::array<std::array<float, 3>, 4>, 2> quads {{
        {{
            {world_x + 0.18F, world_y + 0.0F, world_z + 0.18F},
            {world_x + 0.18F, world_y + 0.95F, world_z + 0.18F},
            {world_x + 0.82F, world_y + 0.95F, world_z + 0.82F},
            {world_x + 0.82F, world_y + 0.0F, world_z + 0.82F},
        }},
        {{
            {world_x + 0.82F, world_y + 0.0F, world_z + 0.18F},
            {world_x + 0.82F, world_y + 0.95F, world_z + 0.18F},
            {world_x + 0.18F, world_y + 0.95F, world_z + 0.82F},
            {world_x + 0.18F, world_y + 0.0F, world_z + 0.82F},
        }},
    }};

    const auto normal_a = std::array<float, 3> {
        0.70710677F, 0.0F, -0.70710677F,
    };
    const auto normal_b = std::array<float, 3> {
        0.70710677F, 0.0F, 0.70710677F,
    };
    const std::array<std::array<float, 3>, 2> front_normals {{normal_a, normal_b}};

    for (std::size_t quad_index = 0; quad_index < quads.size(); ++quad_index) {
        append_cross_quad(mesh, tile, quads[quad_index], front_normals[quad_index], 0.95F, 1.0F, sky_light, block_light, material_class);

        std::array<std::array<float, 3>, 4> reversed_positions {{
            quads[quad_index][3],
            quads[quad_index][2],
            quads[quad_index][1],
            quads[quad_index][0],
        }};
        const auto back_normal = std::array<float, 3> {
            -front_normals[quad_index][0],
            -front_normals[quad_index][1],
            -front_normals[quad_index][2],
        };
        append_cross_quad(mesh, tile, reversed_positions, back_normal, 0.90F, 1.0F, sky_light, block_light, material_class);
    }
}

void append_water_face(ChunkMeshData& mesh,
                       const Neighborhood& neighborhood,
                       const FaceDefinition& definition,
                       Face face,
                       const BlockCoord& local_coord,
                       int chunk_world_x,
                       int chunk_world_z,
                       float top_height,
                       bool surface_block) {
    const auto tile = block_atlas_tile(to_block_id(BlockType::Water), to_visual_face(face));
    const auto uv_step = 1.0F / kBlockAtlasTilesPerAxis;
    const auto u0 = static_cast<float>(tile.x) * uv_step;
    const auto v0 = static_cast<float>(tile.y) * uv_step;
    const auto u1 = u0 + uv_step;
    const auto v1 = v0 + uv_step;
    const auto is_horizontal_water_face = face == Face::PositiveY || face == Face::NegativeY;
    const auto subdivisions = water_face_subdivisions(face);
    const auto material_class = block_visual_material_value(to_block_id(BlockType::Water));
    const auto face_shade = face == Face::PositiveY ? 1.02F : definition.face_shade;

    std::array<Float3, 4> corner_positions {};
    std::array<float, 4> corner_sky_values {};
    std::array<float, 4> corner_block_values {};
    const std::array<Float2, 4> corner_uvs {{{u1, v0}, {u1, v1}, {u0, v1}, {u0, v0}}};

    for (std::size_t i = 0; i < definition.vertices.size(); ++i) {
        const auto& vertex = definition.vertices[i];
        const auto top_vertex = vertex.position[1] > 0.5F;
        const auto y_position = top_vertex ? top_height : 0.0F;
        corner_positions[i] = {
            static_cast<float>(chunk_world_x + local_coord.x) + vertex.position[0],
            static_cast<float>(local_coord.y) + y_position,
            static_cast<float>(chunk_world_z + local_coord.z) + vertex.position[2],
        };

        const auto [sky_light, block_light] = sample_vertex_light(neighborhood, local_coord, definition, vertex);
        corner_sky_values[i] = sky_light;
        corner_block_values[i] = block_light;
    }

    const auto base_index = static_cast<std::uint32_t>(mesh.water_vertices.size());
    const auto row_stride = static_cast<std::uint32_t>(subdivisions.u + 1);

    for (int v_index = 0; v_index <= subdivisions.v; ++v_index) {
        const auto v_lerp = static_cast<float>(v_index) / static_cast<float>(subdivisions.v);
        for (int u_index = 0; u_index <= subdivisions.u; ++u_index) {
            const auto u_lerp = static_cast<float>(u_index) / static_cast<float>(subdivisions.u);
            const auto position = bilerp_vec3(corner_positions, u_lerp, v_lerp);
            auto uv = bilerp_vec2(corner_uvs, u_lerp, v_lerp);
            if (is_horizontal_water_face) {
                const auto block_world_x = chunk_world_x + local_coord.x;
                const auto block_world_z = chunk_world_z + local_coord.z;
                uv = repeating_water_surface_uv(
                    tile,
                    block_world_x,
                    block_world_z,
                    position[0] - static_cast<float>(block_world_x),
                    position[2] - static_cast<float>(block_world_z));
            }

            float wave_weight = 0.0F;
            if (surface_block) {
                if (face == Face::PositiveY) {
                    wave_weight = 1.0F;
                } else if (face != Face::NegativeY) {
                    const auto local_y = position[1] - static_cast<float>(local_coord.y);
                    if (local_y > top_height - 0.0001F) {
                        wave_weight = 1.0F;
                    }
                }
            }

            mesh.water_vertices.push_back({
                position[0],
                position[1],
                position[2],
                uv[0],
                uv[1],
                definition.normal[0],
                definition.normal[1],
                definition.normal[2],
                face_shade,
                1.0F,
                bilerp_float(corner_sky_values, u_lerp, v_lerp),
                bilerp_float(corner_block_values, u_lerp, v_lerp),
                material_class,
                wave_weight,
            });
        }
    }

    const auto diagonal_seed = local_coord.x + local_coord.y + local_coord.z + static_cast<int>(face);
    for (int v_index = 0; v_index < subdivisions.v; ++v_index) {
        for (int u_index = 0; u_index < subdivisions.u; ++u_index) {
            const auto cell_base_index =
                base_index + static_cast<std::uint32_t>(v_index * (subdivisions.u + 1) + u_index);
            const auto index0 = cell_base_index;
            const auto index1 = cell_base_index + 1U;
            const auto index3 = cell_base_index + row_stride;
            const auto index2 = index3 + 1U;
            const auto flip_diagonal = ((diagonal_seed + u_index + v_index) & 1) != 0;
            append_quad_indices(mesh.water_indices, index0, index1, index2, index3, flip_diagonal);
        }
    }

    ++mesh.water_face_count;
}

void append_water_mesh(ChunkMeshData& mesh,
                       const Neighborhood& neighborhood,
                       const BlockCoord& local_coord,
                       int chunk_world_x,
                       int chunk_world_z) {
    const auto block_above = neighborhood.block_at(local_coord.x, local_coord.y + 1, local_coord.z);
    const auto surface_block = !is_block_liquid(block_above);
    const auto top_height = surface_block ? kWaterSurfaceHeight : 1.0F;

    for (int face_index = 0; face_index < static_cast<int>(kFaceDefinitions.size()); ++face_index) {
        const auto face = static_cast<Face>(face_index);
        const auto& definition = kFaceDefinitions[static_cast<std::size_t>(face)];
        const auto neighbor = BlockCoord {
            local_coord.x + definition.neighbor_offset.x,
            local_coord.y + definition.neighbor_offset.y,
            local_coord.z + definition.neighbor_offset.z,
        };
        const auto neighbor_block = neighborhood.block_at(neighbor.x, neighbor.y, neighbor.z);
        if (is_block_liquid(neighbor_block) || is_block_opaque(neighbor_block)) {
            continue;
        }

        append_water_face(mesh, neighborhood, definition, face, local_coord, chunk_world_x, chunk_world_z, top_height, surface_block);
    }
}

} // namespace

auto ChunkMesher::build_mesh(const World& world,
                             const ChunkCoord& coord,
                             std::size_t vertex_reserve_hint,
                             std::size_t index_reserve_hint) const -> ChunkMeshData {
    const auto* chunk = world.find_chunk(coord);
    if (chunk == nullptr || !chunk->has_meshable_blocks()) {
        return {};
    }

    return build_mesh_range(
        world,
        coord,
        chunk->min_mesh_y(),
        chunk->max_mesh_y(),
        vertex_reserve_hint,
        index_reserve_hint);
}

auto ChunkMesher::build_mesh_range(const World& world,
                                   const ChunkCoord& coord,
                                   int min_y,
                                   int max_y,
                                   std::size_t vertex_reserve_hint,
                                   std::size_t index_reserve_hint) const -> ChunkMeshData {
    ChunkMeshData mesh {};
    const auto* chunk = world.find_chunk(coord);
    if (chunk == nullptr) {
        return mesh;
    }
    if (!chunk->has_meshable_blocks()) {
        return mesh;
    }

    const auto clamped_min_y = std::max(chunk->min_mesh_y(), min_y);
    const auto clamped_max_y = std::min(chunk->max_mesh_y(), max_y);
    if (clamped_max_y < clamped_min_y) {
        return mesh;
    }

    mesh.vertices.reserve(vertex_reserve_hint > 0 ? vertex_reserve_hint : 2048U);
    mesh.indices.reserve(index_reserve_hint > 0 ? index_reserve_hint : 3072U);
    mesh.water_vertices.reserve(2048U);
    mesh.water_indices.reserve(4096U);

    const auto cached_min_y = std::max(kWorldMinY, clamped_min_y - 1);
    const auto cached_max_y = std::min(kWorldMaxY, clamped_max_y + 1);
    const auto neighborhood = build_neighborhood(world, coord, cached_min_y, cached_max_y);
    const auto chunk_world_x = coord.x * kChunkSizeX;
    const auto chunk_world_z = coord.z * kChunkSizeZ;
    const auto& chunk_blocks = chunk->blocks();

    for (int y = clamped_min_y; y <= clamped_max_y; ++y) {
        for (int z = 0; z < kChunkSizeZ; ++z) {
            for (int x = 0; x < kChunkSizeX; ++x) {
                const auto block_id = chunk_blocks[chunk_linear_index(x, y, z)];
                if (!has_block_mesh(block_id)) {
                    continue;
                }

                const BlockCoord local_coord {x, y, z};
                if (block_mesh_type(block_id) == BlockMeshType::Torch) {
                    append_torch_mesh(mesh, neighborhood, local_coord, chunk_world_x, chunk_world_z);
                    continue;
                }
                if (block_mesh_type(block_id) == BlockMeshType::Cross) {
                    append_cross_mesh(mesh, neighborhood, block_id, local_coord, chunk_world_x, chunk_world_z);
                    continue;
                }
                if (block_mesh_type(block_id) == BlockMeshType::Water) {
                    append_water_mesh(mesh, neighborhood, local_coord, chunk_world_x, chunk_world_z);
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
