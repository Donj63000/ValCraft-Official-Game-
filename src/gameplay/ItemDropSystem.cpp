#include "gameplay/ItemDropSystem.h"

#include "gameplay/SeaAdventure.h"

#include <glm/common.hpp>
#include <glm/geometric.hpp>

#include <algorithm>
#include <cmath>
#include <limits>
#include <numbers>

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
constexpr float kSleepVelocityThreshold = 0.04F;
constexpr float kSleepDelaySeconds = 0.35F;
constexpr float kSleepSupportCheckInterval = 0.50F;
constexpr float kMaximumDropWorldCoordinateMagnitude = 1'000'000.0F;
constexpr float kMaximumLoadedDropSpeed = 64.0F;
constexpr float kMaximumLoadedPickupCooldownSeconds = 1.0F;
// Les frequences 3,2 et 1,9 retrouvent ensemble leur phase apres 20 pi
// secondes; je peux donc borner l'age sans saut visuel.
constexpr float kDropAnimationCycleSeconds = 20.0F * std::numbers::pi_v<float>;
constexpr std::size_t kMaxActiveDrops = 128;

auto finite_or(float value, float fallback) noexcept -> float {
    return std::isfinite(value) ? value : fallback;
}

auto non_negative_finite(float value) noexcept -> float {
    return std::isfinite(value) ? std::max(value, 0.0F) : 0.0F;
}

auto normalized_drop_age(float value) noexcept -> float {
    if (!std::isfinite(value) || value <= 0.0F) {
        return 0.0F;
    }
    return std::fmod(value, kDropAnimationCycleSeconds);
}

auto sanitized_pickup_cooldown(float value) noexcept -> float {
    return std::clamp(finite_or(value, 0.0F), 0.0F, kMaximumLoadedPickupCooldownSeconds);
}

auto is_finite_vec3(const glm::vec3& value) noexcept -> bool {
    return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z);
}

auto finite_vec3_or(const glm::vec3& value, const glm::vec3& fallback) noexcept -> glm::vec3 {
    return {
        finite_or(value.x, fallback.x),
        finite_or(value.y, fallback.y),
        finite_or(value.z, fallback.z),
    };
}

auto sanitized_drop_velocity(const glm::vec3& velocity) noexcept -> glm::vec3 {
    return glm::clamp(
        finite_vec3_or(velocity, {}),
        glm::vec3 {-kMaximumLoadedDropSpeed},
        glm::vec3 {kMaximumLoadedDropSpeed});
}

auto drop_physics_block(const World& world, int x, int y, int z) -> BlockId {
    // Je garde les drops bloques par le terrain deterministe avant meme que le
    // chunk soit charge, sinon ils peuvent tomber sous le sol pendant le streaming.
    return world.peek_block_or_generated(x, y, z);
}

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
                if (is_block_collidable(drop_physics_block(world, x, y, z))) {
                    return true;
                }
            }
        }
    }

    return false;
}

void move_drop_axis(ItemDrop& drop,
                    float delta,
                    int axis,
                    const World& world,
                    const ShipEntity* dynamic_platform) {
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
                if (!is_block_collidable(drop_physics_block(world, block_x, y, z))) {
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
                if (!is_block_collidable(drop_physics_block(world, x, block_y, z))) {
                    continue;
                }
                if (delta > 0.0F) {
                    next_position.y = static_cast<float>(block_y) - kDropHeight - kDropCollisionEpsilon;
                } else {
                    next_position.y = static_cast<float>(block_y + 1) + kDropCollisionEpsilon;
                    drop.grounded = true;
                    drop.sleep_support_valid = true;
                    drop.sleep_support_block = {x, block_y, z};
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
                if (!is_block_collidable(drop_physics_block(world, x, y, block_z))) {
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

    if (dynamic_platform != nullptr) {
        // Je traite le pont comme un support mobile exact avant les autres
        // collisions du navire afin que le drop ne traverse pas le plancher.
        if (axis == 1 && delta < 0.0F) {
            const auto support_height = dynamic_platform->support_height(next_position);
            if (support_height.has_value() && drop.position.y >= *support_height - kDropCollisionEpsilon &&
                next_position.y <= *support_height + kDropCollisionEpsilon) {
                next_position.y = *support_height + kDropCollisionEpsilon;
                drop.velocity.y = 0.0F;
                drop.grounded = true;
                drop.sleep_support_valid = false;
                drop.position = next_position;
                return;
            }
        }

        const auto intersects_dynamic_platform = [&](const glm::vec3& candidate_position) noexcept {
            const auto obstacle_min = glm::vec3 {
                candidate_position.x - kDropHalfWidth,
                candidate_position.y,
                candidate_position.z - kDropHalfWidth,
            };
            const auto obstacle_max = glm::vec3 {
                candidate_position.x + kDropHalfWidth,
                candidate_position.y + kDropHeight,
                candidate_position.z + kDropHalfWidth,
            };
            return dynamic_platform->intersects_aabb(obstacle_min, obstacle_max);
        };

        if (intersects_dynamic_platform(next_position)) {
            auto safe_fraction = 0.0F;
            auto colliding_fraction = 1.0F;
            for (int iteration = 0; iteration < 8; ++iteration) {
                const auto candidate_fraction = (safe_fraction + colliding_fraction) * 0.5F;
                auto candidate_position = drop.position;
                candidate_position[axis] += delta * candidate_fraction;
                if (intersects_dynamic_platform(candidate_position)) {
                    colliding_fraction = candidate_fraction;
                } else {
                    safe_fraction = candidate_fraction;
                }
            }
            next_position = drop.position;
            next_position[axis] += delta * safe_fraction;
            drop.velocity[axis] = 0.0F;
            if (axis == 1 && delta < 0.0F) {
                drop.grounded = true;
                drop.sleep_support_valid = false;
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

auto is_sane_item_drop_position(const glm::vec3& position) noexcept -> bool {
    return is_finite_vec3(position) &&
           std::abs(position.x) <= kMaximumDropWorldCoordinateMagnitude &&
           std::abs(position.y) <= kMaximumDropWorldCoordinateMagnitude &&
           std::abs(position.z) <= kMaximumDropWorldCoordinateMagnitude;
}

auto sanitize_item_drop_state(ItemDrop& drop) noexcept -> bool {
    normalize_item_stack(drop.stack);
    if (!inventory_slot_has_item(drop.stack) || !is_sane_item_drop_position(drop.position)) {
        return false;
    }

    drop.velocity = sanitized_drop_velocity(drop.velocity);
    drop.age_seconds = normalized_drop_age(drop.age_seconds);
    drop.pickup_cooldown = sanitized_pickup_cooldown(drop.pickup_cooldown);
    drop.sleeping = false;
    drop.sleep_support_valid = false;
    drop.sleep_candidate_seconds = 0.0F;
    drop.sleep_support_check_timer = 0.0F;
    drop.sleep_support_block = {};
    return true;
}

ItemDropSystem::ItemDropSystem() {
    // Je borne et reserve le stockage une seule fois pour qu'aucun spawn en jeu
    // ne declenche de reallocation du tableau de drops.
    drops_.reserve(kMaxActiveDrops);
}

void ItemDropSystem::spawn_drop(const HotbarSlot& stack, const glm::vec3& position, const glm::vec3& initial_velocity) {
    HotbarSlot remaining = stack;
    normalize_item_stack(remaining);
    if (!inventory_slot_has_item(remaining)) {
        return;
    }
    if (!is_sane_item_drop_position(position)) {
        ++audit_stats_.rejected_spawns;
        return;
    }
    const auto safe_initial_velocity = sanitized_drop_velocity(initial_velocity);

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
            ++audit_stats_.merged;
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
                ++audit_stats_.merged;
                return;
            }
        }
        if (drops_.size() >= kMaxActiveDrops) {
            ++audit_stats_.rejected_spawns;
            return;
        }
    }

    ItemDrop drop {};
    drop.position = position;
    drop.velocity = safe_initial_velocity;
    drop.stack = remaining;
    drop.pickup_cooldown = kPickupDelaySeconds;
    drops_.push_back(drop);
    ++audit_stats_.spawned;
    audit_stats_.active_drops = drops_.size();
}

void ItemDropSystem::update(float dt,
                            const World& world,
                            const glm::vec3& player_position,
                            InventoryMenuState& inventory,
                            HotbarState& hotbar,
                            const ShipEntity* dynamic_platform) {
    const auto clamped_dt = non_negative_finite(dt);
    const auto pickup_radius_sq = kPickupRadius * kPickupRadius;
    const auto magnet_radius_sq = kMagnetRadius * kMagnetRadius;
    const auto player_position_is_finite = is_finite_vec3(player_position);

    for (auto iterator = drops_.begin(); iterator != drops_.end();) {
        auto& drop = *iterator;
        normalize_item_stack(drop.stack);
        if (!inventory_slot_has_item(drop.stack) || !is_sane_item_drop_position(drop.position) || drop.position.y < -8.0F) {
            ++audit_stats_.expired;
            iterator = drops_.erase(iterator);
            continue;
        }
        drop.velocity = sanitized_drop_velocity(drop.velocity);
        drop.age_seconds = normalized_drop_age(drop.age_seconds);
        drop.pickup_cooldown = sanitized_pickup_cooldown(drop.pickup_cooldown);
        drop.sleep_candidate_seconds = non_negative_finite(drop.sleep_candidate_seconds);
        drop.sleep_support_check_timer = non_negative_finite(drop.sleep_support_check_timer);

        drop.age_seconds = normalized_drop_age(drop.age_seconds + clamped_dt);
        drop.pickup_cooldown = std::max(0.0F, drop.pickup_cooldown - clamped_dt);

        if (dynamic_platform != nullptr) {
            const auto previous_support =
                dynamic_platform->previous_support_height(
                    drop.position);
            if (previous_support.has_value() &&
                std::abs(
                    drop.position.y -
                    *previous_support) <= 0.03F) {
                // Le point est d'abord transporte par la pose rigide complete.
                // Un second sondage le recale ensuite sur le pont incline.
                auto carried_position =
                    drop.position +
                    dynamic_platform->motion_delta_at(
                        drop.position);
                if (const auto current_support =
                        dynamic_platform->support_height(
                            carried_position);
                    current_support.has_value()) {
                    carried_position.y =
                        *current_support +
                        kDropCollisionEpsilon;
                    drop.position =
                        carried_position;
                    drop.velocity = {};
                    drop.sleeping = false;
                    drop.grounded = true;
                    drop.sleep_support_valid = false;
                    drop.sleep_candidate_seconds = 0.0F;
                }
            }
        }

        const auto to_player = player_position_is_finite ? player_position - drop.position : glm::vec3 {};
        const auto distance_sq = player_position_is_finite ? glm::dot(to_player, to_player) : std::numeric_limits<float>::max();
        if (player_position_is_finite && drop.pickup_cooldown <= 0.0F && distance_sq <= pickup_radius_sq) {
            drop.stack = inventory_try_store_stack(inventory, hotbar, drop.stack);
            if (!inventory_slot_has_item(drop.stack)) {
                ++audit_stats_.picked_up;
                iterator = drops_.erase(iterator);
                continue;
            }
        }

        const auto magnet_active = player_position_is_finite &&
                                   drop.pickup_cooldown <= 0.0F &&
                                   distance_sq <= magnet_radius_sq;
        if (drop.sleeping) {
            auto should_wake = magnet_active;
            drop.sleep_support_check_timer += clamped_dt;
            if (!should_wake && drop.sleep_support_check_timer >= kSleepSupportCheckInterval) {
                drop.sleep_support_check_timer = 0.0F;
                ++audit_stats_.support_checks;
                should_wake = !drop.sleep_support_valid ||
                              !is_block_collidable(drop_physics_block(
                                  world,
                                  drop.sleep_support_block.x,
                                  drop.sleep_support_block.y,
                                  drop.sleep_support_block.z));
            }

            if (!should_wake) {
                drop.velocity = {};
                drop.grounded = true;
                ++iterator;
                continue;
            }

            // Je reveille immediatement le drop si le joueur l'attire, et au
            // prochain controle borne si son bloc support a disparu.
            drop.sleeping = false;
            drop.grounded = false;
            drop.sleep_support_valid = false;
            drop.sleep_candidate_seconds = 0.0F;
            ++audit_stats_.woken_drops;
        }

        if (magnet_active && distance_sq > 1.0e-5F) {
            const auto distance = std::sqrt(distance_sq);
            const auto direction = to_player / distance;
            const auto pull = glm::clamp(7.0F - distance * 1.7F, 0.0F, 7.0F);
            drop.velocity += direction * (pull * clamped_dt);
        }

        drop.grounded = false;
        if (drop_collides_at(world, drop.position)) {
            drop.position.y += 0.02F;
        }

        drop.velocity.y = std::max(drop.velocity.y - kDropGravity * clamped_dt, -kDropTerminalVelocity);
        move_drop_axis(drop, drop.velocity.x * clamped_dt, 0, world, dynamic_platform);
        move_drop_axis(drop, drop.velocity.y * clamped_dt, 1, world, dynamic_platform);
        move_drop_axis(drop, drop.velocity.z * clamped_dt, 2, world, dynamic_platform);
        ++audit_stats_.physics_updates;

        const auto friction = drop.grounded ? kGroundFriction : kAirFriction;
        drop.velocity.x *= friction;
        drop.velocity.z *= friction;

        const auto horizontal_velocity_sq =
            drop.velocity.x * drop.velocity.x + drop.velocity.z * drop.velocity.z;
        if (drop.grounded &&
            std::abs(drop.velocity.y) <= kSleepVelocityThreshold &&
            horizontal_velocity_sq <= kSleepVelocityThreshold * kSleepVelocityThreshold &&
            !magnet_active) {
            drop.sleep_candidate_seconds += clamped_dt;
            if (drop.sleep_candidate_seconds >= kSleepDelaySeconds && drop.sleep_support_valid) {
                drop.sleeping = true;
                drop.velocity = {};
                drop.sleep_support_check_timer = 0.0F;
            }
        } else {
            drop.sleep_candidate_seconds = 0.0F;
            if (!drop.grounded) {
                drop.sleep_support_valid = false;
            }
        }

        const auto pickup_offset = player_position_is_finite ? player_position - drop.position : glm::vec3 {};
        if (player_position_is_finite && drop.pickup_cooldown <= 0.0F && glm::dot(pickup_offset, pickup_offset) <= pickup_radius_sq) {
            drop.stack = inventory_try_store_stack(inventory, hotbar, drop.stack);
            if (!inventory_slot_has_item(drop.stack)) {
                ++audit_stats_.picked_up;
                iterator = drops_.erase(iterator);
                continue;
            }
        }

        ++iterator;
    }

    audit_stats_.active_drops = drops_.size();
    audit_stats_.sleeping_drops = static_cast<std::size_t>(
        std::count_if(drops_.begin(), drops_.end(), [](const ItemDrop& drop) { return drop.sleeping; }));
}

void ItemDropSystem::build_render_instances(const World& world, std::vector<ItemDropRenderInstance>& out) const {
    out.clear();
    out.reserve(drops_.size());

    for (const auto& drop : drops_) {
        if (!inventory_slot_has_item(drop.stack) || !is_sane_item_drop_position(drop.position)) {
            continue;
        }

        const auto animation_age = normalized_drop_age(drop.age_seconds);
        out.push_back({
            drop.position,
            drop.stack.block_id,
            drop.stack.count,
            animation_age,
            animation_age * 1.9F,
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

auto ItemDropSystem::consume_audit_stats() noexcept -> ItemDropAuditStats {
    const auto stats = audit_stats_;
    audit_stats_ = {};
    audit_stats_.active_drops = drops_.size();
    audit_stats_.sleeping_drops = static_cast<std::size_t>(
        std::count_if(drops_.begin(), drops_.end(), [](const ItemDrop& drop) { return drop.sleeping; }));
    return stats;
}

void ItemDropSystem::load_drops(const std::vector<ItemDrop>& drops) {
    drops_.clear();
    drops_.reserve(std::min(drops.size(), kMaxActiveDrops));
    for (auto drop : drops) {
        if (!sanitize_item_drop_state(drop)) {
            continue;
        }
        drops_.push_back(drop);
        if (drops_.size() >= kMaxActiveDrops) {
            break;
        }
    }
    audit_stats_ = {};
    audit_stats_.active_drops = drops_.size();
    audit_stats_.sleeping_drops = 0;
}

void ItemDropSystem::clear() noexcept {
    drops_.clear();
    audit_stats_ = {};
}

} // namespace valcraft
