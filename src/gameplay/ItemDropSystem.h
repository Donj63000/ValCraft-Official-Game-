#pragma once

#include "app/InventoryMenu.h"
#include "world/World.h"

#include <glm/vec3.hpp>

#include <cstddef>
#include <cstdint>
#include <vector>

namespace valcraft {

struct ItemDrop {
    glm::vec3 position {0.0F};
    glm::vec3 velocity {0.0F};
    HotbarSlot stack {};
    float age_seconds = 0.0F;
    float pickup_cooldown = 0.0F;
    bool grounded = false;
};

struct ItemDropRenderInstance {
    glm::vec3 position {0.0F};
    BlockId block_id = to_block_id(BlockType::Air);
    std::uint8_t count = 0;
    float age_seconds = 0.0F;
    float spin_radians = 0.0F;
    float sky_light = 1.0F;
    float block_light = 0.0F;
};

class ItemDropSystem {
public:
    void spawn_drop(const HotbarSlot& stack, const glm::vec3& position, const glm::vec3& initial_velocity);
    void update(float dt,
                const World& world,
                const glm::vec3& player_position,
                InventoryMenuState& inventory,
                HotbarState& hotbar);
    void build_render_instances(const World& world, std::vector<ItemDropRenderInstance>& out) const;

    [[nodiscard]] auto active_drop_count() const noexcept -> std::size_t;
    [[nodiscard]] auto drops() const noexcept -> const std::vector<ItemDrop>&;

private:
    std::vector<ItemDrop> drops_ {};
};

} // namespace valcraft
