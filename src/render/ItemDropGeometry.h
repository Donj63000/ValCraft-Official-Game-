#pragma once

#include "gameplay/ItemDropSystem.h"
#include "render/VisualPipeline.h"
#include "world/ChunkMesher.h"
#include "world/BlockVisuals.h"

#include <glm/mat4x4.hpp>
#include <glm/vec4.hpp>

#include <span>
#include <vector>

namespace valcraft {

struct ItemDropGpuInstance {
    glm::mat4 transform {1.0F};
    BlockId block_id = to_block_id(BlockType::Air);
    BlockId texture_id = to_block_id(BlockType::Air);
    float sky_light = 1.0F;
    float block_light = 0.0F;
    float material_class = 0.0F;
    glm::vec4 face_tiles_0_1 {0.0F};
    glm::vec4 face_tiles_2_3 {0.0F};
    glm::vec4 face_tiles_4_5 {0.0F};
};

inline constexpr float kMusketGroundDropScale = 0.72F;

[[nodiscard]] constexpr auto item_drop_uses_rounded_template(
    VisualPipeline pipeline) noexcept -> bool {
    return pipeline == VisualPipeline::ModernStylized;
}

// Je centralise les tuiles des drops: Air ou id inconnu ne doit produire aucune instance.
[[nodiscard]] constexpr auto item_drop_atlas_tile(BlockId block_id, BlockVisualFace face) noexcept -> BlockAtlasTile {
    const auto item_id = block_item_id(block_id);
    if (item_id == to_block_id(BlockType::Air)) {
        return {};
    }
    return block_atlas_tile(item_id, face);
}

void build_item_drop_gpu_instances_into(std::span<const ItemDropRenderInstance> item_drops,
                                        std::vector<ItemDropGpuInstance>& instances);

[[nodiscard]] auto build_item_drop_gpu_instances(std::span<const ItemDropRenderInstance> item_drops)
    -> std::vector<ItemDropGpuInstance>;

void build_item_drop_vertices_into(std::span<const ItemDropRenderInstance> item_drops, std::vector<ChunkVertex>& vertices);

[[nodiscard]] auto build_item_drop_vertices(std::span<const ItemDropRenderInstance> item_drops)
    -> std::vector<ChunkVertex>;

} // namespace valcraft
