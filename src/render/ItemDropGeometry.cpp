#include "render/ItemDropGeometry.h"

#include "world/BlockVisuals.h"

#include <glm/vec3.hpp>

#include <array>
#include <cmath>

namespace valcraft {

namespace {

auto atlas_uv_rect(const BlockAtlasTile& tile) -> std::array<float, 4> {
    const auto uv_step = 1.0F / kBlockAtlasTilesPerAxis;
    const auto u0 = static_cast<float>(tile.x) * uv_step;
    const auto v0 = static_cast<float>(tile.y) * uv_step;
    return {u0, v0, u0 + uv_step, v0 + uv_step};
}

void append_item_drop_face(std::vector<ChunkVertex>& vertices,
                           const std::array<glm::vec3, 4>& positions,
                           const glm::vec3& normal,
                           float face_shade,
                           const std::array<float, 4>& uv_rect,
                           float sky_light,
                           float block_light,
                           float material_class) {
    const auto u0 = uv_rect[0];
    const auto v0 = uv_rect[1];
    const auto u1 = uv_rect[2];
    const auto v1 = uv_rect[3];

    vertices.insert(vertices.end(), {
        {positions[0].x, positions[0].y, positions[0].z, u0, v1, normal.x, normal.y, normal.z, face_shade, 1.0F, sky_light, block_light, material_class},
        {positions[1].x, positions[1].y, positions[1].z, u1, v1, normal.x, normal.y, normal.z, face_shade, 1.0F, sky_light, block_light, material_class},
        {positions[2].x, positions[2].y, positions[2].z, u1, v0, normal.x, normal.y, normal.z, face_shade, 1.0F, sky_light, block_light, material_class},
        {positions[0].x, positions[0].y, positions[0].z, u0, v1, normal.x, normal.y, normal.z, face_shade, 1.0F, sky_light, block_light, material_class},
        {positions[2].x, positions[2].y, positions[2].z, u1, v0, normal.x, normal.y, normal.z, face_shade, 1.0F, sky_light, block_light, material_class},
        {positions[3].x, positions[3].y, positions[3].z, u0, v0, normal.x, normal.y, normal.z, face_shade, 1.0F, sky_light, block_light, material_class},
    });
}

auto rotate_item_drop_vector(const glm::vec3& value, float cos_rotation, float sin_rotation) -> glm::vec3 {
    return {
        value.x * cos_rotation - value.z * sin_rotation,
        value.y,
        value.x * sin_rotation + value.z * cos_rotation,
    };
}

void append_item_drop_cube(std::vector<ChunkVertex>& vertices,
                           const glm::vec3& center,
                           float size,
                           float rotation,
                           BlockId block_id,
                           float sky_light,
                           float block_light,
                           float material_class) {
    const auto half_extent = size * 0.5F;
    const auto cos_rotation = std::cos(rotation);
    const auto sin_rotation = std::sin(rotation);
    const auto make_position = [&](float x, float y, float z) {
        return center + rotate_item_drop_vector({x, y, z}, cos_rotation, sin_rotation);
    };
    const auto rotate_normal = [&](float x, float y, float z) {
        return rotate_item_drop_vector({x, y, z}, cos_rotation, sin_rotation);
    };

    append_item_drop_face(
        vertices,
        {{
            make_position(half_extent, -half_extent, -half_extent),
            make_position(half_extent, -half_extent, half_extent),
            make_position(half_extent, half_extent, half_extent),
            make_position(half_extent, half_extent, -half_extent),
        }},
        rotate_normal(1.0F, 0.0F, 0.0F),
        0.85F,
        atlas_uv_rect(block_atlas_tile(block_id, BlockVisualFace::PositiveX)),
        sky_light,
        block_light,
        material_class);

    append_item_drop_face(
        vertices,
        {{
            make_position(-half_extent, -half_extent, half_extent),
            make_position(-half_extent, -half_extent, -half_extent),
            make_position(-half_extent, half_extent, -half_extent),
            make_position(-half_extent, half_extent, half_extent),
        }},
        rotate_normal(-1.0F, 0.0F, 0.0F),
        0.85F,
        atlas_uv_rect(block_atlas_tile(block_id, BlockVisualFace::NegativeX)),
        sky_light,
        block_light,
        material_class);

    append_item_drop_face(
        vertices,
        {{
            make_position(-half_extent, half_extent, half_extent),
            make_position(half_extent, half_extent, half_extent),
            make_position(half_extent, half_extent, -half_extent),
            make_position(-half_extent, half_extent, -half_extent),
        }},
        rotate_normal(0.0F, 1.0F, 0.0F),
        1.0F,
        atlas_uv_rect(block_atlas_tile(block_id, BlockVisualFace::PositiveY)),
        sky_light,
        block_light,
        material_class);

    append_item_drop_face(
        vertices,
        {{
            make_position(-half_extent, -half_extent, -half_extent),
            make_position(half_extent, -half_extent, -half_extent),
            make_position(half_extent, -half_extent, half_extent),
            make_position(-half_extent, -half_extent, half_extent),
        }},
        rotate_normal(0.0F, -1.0F, 0.0F),
        0.65F,
        atlas_uv_rect(block_atlas_tile(block_id, BlockVisualFace::NegativeY)),
        sky_light,
        block_light,
        material_class);

    append_item_drop_face(
        vertices,
        {{
            make_position(half_extent, -half_extent, half_extent),
            make_position(-half_extent, -half_extent, half_extent),
            make_position(-half_extent, half_extent, half_extent),
            make_position(half_extent, half_extent, half_extent),
        }},
        rotate_normal(0.0F, 0.0F, 1.0F),
        0.75F,
        atlas_uv_rect(block_atlas_tile(block_id, BlockVisualFace::PositiveZ)),
        sky_light,
        block_light,
        material_class);

    append_item_drop_face(
        vertices,
        {{
            make_position(-half_extent, -half_extent, -half_extent),
            make_position(half_extent, -half_extent, -half_extent),
            make_position(half_extent, half_extent, -half_extent),
            make_position(-half_extent, half_extent, -half_extent),
        }},
        rotate_normal(0.0F, 0.0F, -1.0F),
        0.75F,
        atlas_uv_rect(block_atlas_tile(block_id, BlockVisualFace::NegativeZ)),
        sky_light,
        block_light,
        material_class);
}

} // namespace

void build_item_drop_gpu_instances_into(std::span<const ItemDropRenderInstance> item_drops,
                                        std::vector<ItemDropGpuInstance>& instances) {
    instances.clear();
    instances.reserve(item_drops.size() * 3U);

    for (const auto& drop : item_drops) {
        if (drop.block_id == to_block_id(BlockType::Air) || drop.count == 0) {
            continue;
        }

        const auto material_class = block_visual_material_value(drop.block_id);
        const auto bob_offset = std::sin(drop.age_seconds * 3.2F) * 0.06F + 0.12F;
        const auto size = drop.count >= 32 ? 0.42F : (drop.count >= 2 ? 0.39F : 0.35F);
        const auto layer_count = drop.count >= 32 ? 3 : (drop.count >= 2 ? 2 : 1);

        for (int layer = 0; layer < layer_count; ++layer) {
            const auto layer_offset = static_cast<float>(layer) * 0.03F;
            const auto lateral_offset = static_cast<float>(layer) * 0.02F;
            instances.push_back({
                drop.position + glm::vec3 {
                    layer == 0 ? 0.0F : lateral_offset,
                    bob_offset + layer_offset + size * 0.5F,
                    layer == 2 ? -lateral_offset : 0.0F,
                },
                size,
                drop.spin_radians,
                block_item_id(drop.block_id),
                drop.sky_light,
                drop.block_light,
                material_class,
            });
        }
    }
}

auto build_item_drop_gpu_instances(std::span<const ItemDropRenderInstance> item_drops) -> std::vector<ItemDropGpuInstance> {
    std::vector<ItemDropGpuInstance> instances;
    build_item_drop_gpu_instances_into(item_drops, instances);
    return instances;
}

void build_item_drop_vertices_into(std::span<const ItemDropRenderInstance> item_drops, std::vector<ChunkVertex>& vertices) {
    std::vector<ItemDropGpuInstance> instances;
    build_item_drop_gpu_instances_into(item_drops, instances);

    vertices.clear();
    vertices.reserve(instances.size() * 36U);

    for (const auto& instance : instances) {
        append_item_drop_cube(
            vertices,
            instance.center,
            instance.size,
            instance.rotation,
            instance.block_id,
            instance.sky_light,
            instance.block_light,
            instance.material_class);
    }
}

auto build_item_drop_vertices(std::span<const ItemDropRenderInstance> item_drops) -> std::vector<ChunkVertex> {
    std::vector<ChunkVertex> vertices;
    build_item_drop_vertices_into(item_drops, vertices);
    return vertices;
}

} // namespace valcraft
