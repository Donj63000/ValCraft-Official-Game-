#pragma once

#include "gameplay/ItemDropSystem.h"
#include "world/ChunkMesher.h"

#include <span>
#include <vector>

namespace valcraft {

struct ItemDropGpuInstance {
    glm::vec3 center {0.0F};
    float size = 0.0F;
    float rotation = 0.0F;
    BlockId block_id = to_block_id(BlockType::Air);
    float sky_light = 1.0F;
    float block_light = 0.0F;
    float material_class = 0.0F;
};

void build_item_drop_gpu_instances_into(std::span<const ItemDropRenderInstance> item_drops,
                                        std::vector<ItemDropGpuInstance>& instances);

[[nodiscard]] auto build_item_drop_gpu_instances(std::span<const ItemDropRenderInstance> item_drops)
    -> std::vector<ItemDropGpuInstance>;

void build_item_drop_vertices_into(std::span<const ItemDropRenderInstance> item_drops, std::vector<ChunkVertex>& vertices);

[[nodiscard]] auto build_item_drop_vertices(std::span<const ItemDropRenderInstance> item_drops)
    -> std::vector<ChunkVertex>;

} // namespace valcraft
