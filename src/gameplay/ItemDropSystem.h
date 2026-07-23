#pragma once

#include "app/InventoryMenu.h"
#include "world/World.h"

#include <glm/vec3.hpp>

#include <cstddef>
#include <cstdint>
#include <vector>

namespace valcraft {

class ShipEntity;

struct ItemDrop {
    glm::vec3 position {0.0F};
    glm::vec3 velocity {0.0F};
    HotbarSlot stack {};
    float age_seconds = 0.0F;
    float pickup_cooldown = 0.0F;
    bool grounded = false;
    bool sleeping = false;
    bool sleep_support_valid = false;
    float sleep_candidate_seconds = 0.0F;
    float sleep_support_check_timer = 0.0F;
    BlockCoord sleep_support_block {};
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

struct ItemDropAuditStats {
    std::size_t spawned = 0;
    std::size_t merged = 0;
    std::size_t picked_up = 0;
    std::size_t expired = 0;
    std::size_t active_drops = 0;
    std::size_t rejected_spawns = 0;
    std::size_t sleeping_drops = 0;
    std::size_t physics_updates = 0;
    std::size_t support_checks = 0;
    std::size_t woken_drops = 0;
};

// Je partage cette validation avec la frontiere de sauvegarde afin qu'aucune
// position finie mais hors plage n'atteigne une conversion voxel en entier.
[[nodiscard]] auto is_sane_item_drop_position(const glm::vec3& position) noexcept -> bool;

// Je normalise aussi les champs persistants avant toute simulation ou
// serialisation afin qu'une ancienne sauvegarde ne puisse injecter de NaN dans
// l'animation, la physique ou le delai de ramassage.
[[nodiscard]] auto sanitize_item_drop_state(ItemDrop& drop) noexcept -> bool;

class ItemDropSystem {
public:
    ItemDropSystem();

    void spawn_drop(const HotbarSlot& stack, const glm::vec3& position, const glm::vec3& initial_velocity);
    void update(float dt,
                const World& world,
                const glm::vec3& player_position,
                InventoryMenuState& inventory,
                HotbarState& hotbar,
                const ShipEntity* dynamic_platform = nullptr);
    void build_render_instances(const World& world, std::vector<ItemDropRenderInstance>& out) const;

    [[nodiscard]] auto active_drop_count() const noexcept -> std::size_t;
    [[nodiscard]] auto drops() const noexcept -> const std::vector<ItemDrop>&;
    [[nodiscard]] auto consume_audit_stats() noexcept -> ItemDropAuditStats;
    void load_drops(const std::vector<ItemDrop>& drops);
    void clear() noexcept;

private:
    std::vector<ItemDrop> drops_ {};
    ItemDropAuditStats audit_stats_ {};
};

} // namespace valcraft
