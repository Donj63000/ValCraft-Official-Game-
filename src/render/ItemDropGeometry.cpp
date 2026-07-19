#include "render/ItemDropGeometry.h"

#include "world/BlockVisuals.h"

#include <glm/vec3.hpp>

#include <array>
#include <cmath>

namespace valcraft {

namespace {

constexpr float kTwoPi = 6.2831853071795864769F;
constexpr float kDropAnimationCycleSeconds = 10.0F * kTwoPi;

auto normalized_animation_value(float value, float cycle) noexcept -> float {
    if (!std::isfinite(value) || value <= 0.0F) {
        return 0.0F;
    }
    return std::fmod(value, cycle);
}

struct ItemDropFaceTilePack {
    glm::vec4 face_tiles_0_1 {0.0F};
    glm::vec4 face_tiles_2_3 {0.0F};
    glm::vec4 face_tiles_4_5 {0.0F};
};

auto atlas_uv_rect(const BlockAtlasTile& tile) -> std::array<float, 4> {
    const auto uv_step = 1.0F / kBlockAtlasTilesPerAxis;
    const auto u0 = static_cast<float>(tile.x) * uv_step;
    const auto v0 = static_cast<float>(tile.y) * uv_step;
    return {u0, v0, u0 + uv_step, v0 + uv_step};
}

auto pack_item_drop_face_tiles(BlockId item_id) -> ItemDropFaceTilePack {
    const auto positive_x = item_drop_atlas_tile(item_id, BlockVisualFace::PositiveX);
    const auto negative_x = item_drop_atlas_tile(item_id, BlockVisualFace::NegativeX);
    const auto positive_y = item_drop_atlas_tile(item_id, BlockVisualFace::PositiveY);
    const auto negative_y = item_drop_atlas_tile(item_id, BlockVisualFace::NegativeY);
    const auto positive_z = item_drop_atlas_tile(item_id, BlockVisualFace::PositiveZ);
    const auto negative_z = item_drop_atlas_tile(item_id, BlockVisualFace::NegativeZ);
    return {
        {static_cast<float>(positive_x.x), static_cast<float>(positive_x.y), static_cast<float>(negative_x.x), static_cast<float>(negative_x.y)},
        {static_cast<float>(positive_y.x), static_cast<float>(positive_y.y), static_cast<float>(negative_y.x), static_cast<float>(negative_y.y)},
        {static_cast<float>(positive_z.x), static_cast<float>(positive_z.y), static_cast<float>(negative_z.x), static_cast<float>(negative_z.y)},
    };
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
        atlas_uv_rect(item_drop_atlas_tile(block_id, BlockVisualFace::PositiveX)),
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
        atlas_uv_rect(item_drop_atlas_tile(block_id, BlockVisualFace::NegativeX)),
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
        atlas_uv_rect(item_drop_atlas_tile(block_id, BlockVisualFace::PositiveY)),
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
        atlas_uv_rect(item_drop_atlas_tile(block_id, BlockVisualFace::NegativeY)),
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
        atlas_uv_rect(item_drop_atlas_tile(block_id, BlockVisualFace::PositiveZ)),
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
        atlas_uv_rect(item_drop_atlas_tile(block_id, BlockVisualFace::NegativeZ)),
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
        const auto item_id = block_item_id(drop.block_id);
        if (item_id == to_block_id(BlockType::Air) || drop.count == 0) {
            continue;
        }

        const auto material_class = block_visual_material_value(item_id);
        const auto face_tiles = pack_item_drop_face_tiles(item_id);
        // Je garde ce dernier garde-fou dans le generateur : meme une instance
        // construite hors du systeme de gameplay ne doit produire aucun NaN GPU.
        const auto animation_age = normalized_animation_value(drop.age_seconds, kDropAnimationCycleSeconds);
        const auto rotation = normalized_animation_value(drop.spin_radians, kTwoPi);
        const auto bob_offset = std::sin(animation_age * 3.2F) * 0.06F + 0.12F;
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
                rotation,
                item_id,
                drop.sky_light,
                drop.block_light,
                material_class,
                face_tiles.face_tiles_0_1,
                face_tiles.face_tiles_2_3,
                face_tiles.face_tiles_4_5,
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
