#include "gameplay/ItemDropSystem.h"

#include <glm/common.hpp>
#include <glm/geometric.hpp>

#include <algorithm>
#include <cmath>

namespace valcraft {

namespace {

constexpr float kDropHalfWidth = 0.15F;
constexpr float kDropHeight = 0.24F;
constexpr float kDropCollisionEpsilon = 0.001F;
constexpr float kDropGravity = 18.0F;
constexpr float kDropTerminalVelocity = 12.0F;
constexpr float kPickupRadius = 1.35F;
constexpr float kMagnetRadius = 2.75F;
constexpr float kPickupDelaySeconds = 0.18F;
constexpr float kMergeRadius = 0.70F;
constexpr float kGroundFriction = 0.82F;
constexpr float kAirFriction = 0.98F;
constexpr std::size_t kMaxActiveDrops = 128;

auto drop_collides_at(const World& world, const glm::vec3& position) -> bool {
    const auto min_corner = glm::vec3 {position.x - kDropHalfWidth, position.y, position.z - kDropHalfWidth};
    const auto max_corner = glm::vec3 {position.x + kDropHalfWidth, position.y + kDropHeight, position.z + kDropHalfWidth};

    const auto min_x = static_cast<int>(std::floor(min_corner.x));
    const auto min_y = static_cast<int>(std::floor(min_corner.y));
    const auto min_z = static_cast<int>(std::floor(min_corner.z));
    const auto max_x = static_cast<int>(std::floor(max_corner.x - kDropCollisionEpsilon));
    const auto max_y = static_cast<int>(std::floor(max_corner.y - kDropCollisionEpsilon));
    const auto max_z = static_cast<int>(std::floor(max_corner.z - kDropCollisionEpsilon));

    for (int y = min_y; y <= max_y; ++y) {
        for (int z = min_z; z <= max_z; ++z) {
            for (int x = min_x; x <= max_x; ++x) {
                if (is_block_collidable(world.get_block(x, y, z))) {
                    return true;
                }
            }
        }
    }

    return false;
}

void move_drop_axis(ItemDrop& drop, float delta, int axis, const World& world) {
    if (std::abs(delta) <= 1.0e-6F) {
        return;
    }

    auto next_position = drop.position;
    next_position[axis] += delta;

    const auto min_corner = glm::vec3 {next_position.x - kDropHalfWidth, next_position.y, next_position.z - kDropHalfWidth};
    const auto max_corner = glm::vec3 {next_position.x + kDropHalfWidth, next_position.y + kDropHeight, next_position.z + kDropHalfWidth};

    if (axis == 0) {
        const auto block_x = delta > 0.0F
                                 ? static_cast<int>(std::floor(max_corner.x - kDropCollisionEpsilon))
                                 : static_cast<int>(std::floor(min_corner.x + kDropCollisionEpsilon));
        const auto min_y = static_cast<int>(std::floor(min_corner.y));
        const auto max_y = static_cast<int>(std::floor(max_corner.y - kDropCollisionEpsilon));
        const auto min_z = static_cast<int>(std::floor(min_corner.z));
        const auto max_z = static_cast<int>(std::floor(max_corner.z - kDropCollisionEpsilon));

        for (int y = min_y; y <= max_y; ++y) {
            for (int z = min_z; z <= max_z; ++z) {
                if (!is_block_collidable(world.get_block(block_x, y, z))) {
                    continue;
                }
                next_position.x = delta > 0.0F
                                      ? static_cast<float>(block_x) - kDropHalfWidth - kDropCollisionEpsilon
                                      : static_cast<float>(block_x + 1) + kDropHalfWidth + kDropCollisionEpsilon;
                drop.velocity.x = 0.0F;
                drop.position = next_position;
                return;
            }
        }
    } else if (axis == 1) {
        const auto block_y = delta > 0.0F
                                 ? static_cast<int>(std::floor(max_corner.y - kDropCollisionEpsilon))
                                 : static_cast<int>(std::floor(min_corner.y + kDropCollisionEpsilon));
        const auto min_x = static_cast<int>(std::floor(min_corner.x));
        const auto max_x = static_cast<int>(std::floor(max_corner.x - kDropCollisionEpsilon));
        const auto min_z = static_cast<int>(std::floor(min_corner.z));
        const auto max_z = static_cast<int>(std::floor(max_corner.z - kDropCollisionEpsilon));

        for (int z = min_z; z <= max_z; ++z) {
            for (int x = min_x; x <= max_x; ++x) {
                if (!is_block_collidable(world.get_block(x, block_y, z))) {
                    continue;
                }
                if (delta > 0.0F) {
                    next_position.y = static_cast<float>(block_y) - kDropHeight - kDropCollisionEpsilon;
                } else {
                    next_position.y = static_cast<float>(block_y + 1) + kDropCollisionEpsilon;
                    drop.grounded = true;
                }
                drop.velocity.y = 0.0F;
                drop.position = next_position;
                return;
            }
        }
    } else {
        const auto block_z = delta > 0.0F
                                 ? static_cast<int>(std::floor(max_corner.z - kDropCollisionEpsilon))
                                 : static_cast<int>(std::floor(min_corner.z + kDropCollisionEpsilon));
        const auto min_x = static_cast<int>(std::floor(min_corner.x));
        const auto max_x = static_cast<int>(std::floor(max_corner.x - kDropCollisionEpsilon));
        const auto min_y = static_cast<int>(std::floor(min_corner.y));
        const auto max_y = static_cast<int>(std::floor(max_corner.y - kDropCollisionEpsilon));

        for (int y = min_y; y <= max_y; ++y) {
            for (int x = min_x; x <= max_x; ++x) {
                if (!is_block_collidable(world.get_block(x, y, block_z))) {
                    continue;
                }
                next_position.z = delta > 0.0F
                                      ? static_cast<float>(block_z) - kDropHalfWidth - kDropCollisionEpsilon
                                      : static_cast<float>(block_z + 1) + kDropHalfWidth + kDropCollisionEpsilon;
                drop.velocity.z = 0.0F;
                drop.position = next_position;
                return;
            }
        }
    }

    drop.position = next_position;
}

auto drop_light_level(const World& world, const glm::vec3& position, bool sky) -> float {
    const auto block_y = static_cast<int>(std::floor(position.y));
    if (!is_world_y_valid(block_y)) {
        return sky ? 1.0F : 0.0F;
    }

    const auto block_x = static_cast<int>(std::floor(position.x));
    const auto block_z = static_cast<int>(std::floor(position.z));
    const auto value = sky
                           ? world.get_sky_light(block_x, block_y, block_z)
                           : world.get_block_light(block_x, block_y, block_z);
    return static_cast<float>(value) / 15.0F;
}

} // namespace

void ItemDropSystem::spawn_drop(const HotbarSlot& stack, const glm::vec3& position, const glm::vec3& initial_velocity) {
    HotbarSlot remaining = stack;
    normalize_item_stack(remaining);
    if (!inventory_slot_has_item(remaining)) {
        return;
    }

    const auto merge_radius_sq = kMergeRadius * kMergeRadius;
    for (auto& drop : drops_) {
        if (!inventory_slot_has_item(drop.stack) || drop.stack.block_id != remaining.block_id) {
            continue;
        }
        const auto offset = drop.position - position;
        if (glm::dot(offset, offset) > merge_radius_sq) {
            continue;
        }

        inventory_merge_into_slot(drop.stack, remaining);
        if (!inventory_slot_has_item(remaining)) {
            return;
        }
    }

    if (drops_.size() >= kMaxActiveDrops) {
        for (auto& drop : drops_) {
            if (!inventory_slot_has_item(drop.stack) || drop.stack.block_id != remaining.block_id) {
                continue;
            }
            inventory_merge_into_slot(drop.stack, remaining);
            if (!inventory_slot_has_item(remaining)) {
                return;
            }
        }
        if (drops_.size() >= kMaxActiveDrops) {
            return;
        }
    }

    drops_.push_back({position, initial_velocity, remaining, 0.0F, kPickupDelaySeconds, false});
}

void ItemDropSystem::update(float dt,
                            const World& world,
                            const glm::vec3& player_position,
                            InventoryMenuState& inventory,
                            HotbarState& hotbar) {
    const auto clamped_dt = std::max(dt, 0.0F);
    const auto pickup_radius_sq = kPickupRadius * kPickupRadius;
    const auto magnet_radius_sq = kMagnetRadius * kMagnetRadius;

    for (auto iterator = drops_.begin(); iterator != drops_.end();) {
        auto& drop = *iterator;
        normalize_item_stack(drop.stack);
        if (!inventory_slot_has_item(drop.stack) || drop.position.y < -8.0F) {
            iterator = drops_.erase(iterator);
            continue;
        }

        drop.age_seconds += clamped_dt;
        drop.pickup_cooldown = std::max(0.0F, drop.pickup_cooldown - clamped_dt);
        drop.grounded = false;

        const auto to_player = player_position - drop.position;
        const auto distance_sq = glm::dot(to_player, to_player);
        if (drop.pickup_cooldown <= 0.0F && distance_sq <= pickup_radius_sq) {
            drop.stack = inventory_try_store_stack(inventory, hotbar, drop.stack);
            if (!inventory_slot_has_item(drop.stack)) {
                iterator = drops_.erase(iterator);
                continue;
            }
        }

        if (drop.pickup_cooldown <= 0.0F && distance_sq <= magnet_radius_sq && distance_sq > 1.0e-5F) {
            const auto distance = std::sqrt(distance_sq);
            const auto direction = to_player / distance;
            const auto pull = glm::clamp(7.0F - distance * 1.7F, 0.0F, 7.0F);
            drop.velocity += direction * (pull * clamped_dt);
        }

        if (drop_collides_at(world, drop.position)) {
            drop.position.y += 0.02F;
        }

        drop.velocity.y = std::max(drop.velocity.y - kDropGravity * clamped_dt, -kDropTerminalVelocity);
        move_drop_axis(drop, drop.velocity.x * clamped_dt, 0, world);
        move_drop_axis(drop, drop.velocity.y * clamped_dt, 1, world);
        move_drop_axis(drop, drop.velocity.z * clamped_dt, 2, world);

        const auto friction = drop.grounded ? kGroundFriction : kAirFriction;
        drop.velocity.x *= friction;
        drop.velocity.z *= friction;

        const auto pickup_offset = player_position - drop.position;
        if (drop.pickup_cooldown <= 0.0F && glm::dot(pickup_offset, pickup_offset) <= pickup_radius_sq) {
            drop.stack = inventory_try_store_stack(inventory, hotbar, drop.stack);
            if (!inventory_slot_has_item(drop.stack)) {
                iterator = drops_.erase(iterator);
                continue;
            }
        }

        ++iterator;
    }
}

void ItemDropSystem::build_render_instances(const World& world, std::vector<ItemDropRenderInstance>& out) const {
    out.clear();
    out.reserve(drops_.size());

    for (const auto& drop : drops_) {
        if (!inventory_slot_has_item(drop.stack)) {
            continue;
        }

        out.push_back({
            drop.position,
            drop.stack.block_id,
            drop.stack.count,
            drop.age_seconds,
            drop.age_seconds * 1.9F,
            drop_light_level(world, drop.position, true),
            drop_light_level(world, drop.position, false),
        });
    }
}

auto ItemDropSystem::active_drop_count() const noexcept -> std::size_t {
    return drops_.size();
}

auto ItemDropSystem::drops() const noexcept -> const std::vector<ItemDrop>& {
    return drops_;
}

} // namespace valcraft
