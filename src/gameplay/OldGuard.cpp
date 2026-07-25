#include "gameplay/OldGuard.h"

#include "creatures/CrewAnimation.h"
#include "creatures/OldGuardAnimation.h"

#include <glm/common.hpp>
#include <glm/geometric.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>

namespace valcraft {

namespace {

constexpr float kPi = 3.14159265358979323846F;
constexpr float kTwoPi = 2.0F * kPi;
constexpr float kOldGuardVisionHalfAngle = 80.0F * kPi / 180.0F;
constexpr float kOldGuardScanAmplitude = 70.0F * kPi / 180.0F;
constexpr float kOldGuardFirePoseSeconds = 0.18F;
constexpr float kOldGuardBodyRadius = 0.38F;
constexpr float kOldGuardTurnSpeed = 5.4F;
constexpr float kOldGuardMaximumSimulationStep = 30.0F;

constexpr std::array<OldGuardPatrol, kOldGuardMemberCount> kPatrols {{
    {
        OldGuardPatrolRoute::AftPort,
        {{
            {-3.50F, 4.51F, -32.20F},
            {-4.10F, 4.51F, -29.10F},
            {-3.55F, 4.51F, -26.20F},
            {-2.25F, 4.51F, -29.00F},
        }},
    },
    {
        OldGuardPatrolRoute::AftStarboard,
        {{
            {3.50F, 4.51F, -32.20F},
            {4.10F, 4.51F, -29.10F},
            {3.55F, 4.51F, -26.20F},
            {2.25F, 4.51F, -29.00F},
        }},
    },
    {
        OldGuardPatrolRoute::MainDeckPort,
        {{
            {-5.85F, 4.01F, -19.50F},
            {-6.25F, 4.01F, -8.00F},
            {-5.75F, 4.01F, 4.50F},
            {-4.85F, 4.01F, 18.50F},
        }},
    },
    {
        OldGuardPatrolRoute::MainDeckStarboard,
        {{
            {5.85F, 4.01F, -19.50F},
            {6.25F, 4.01F, -8.00F},
            {5.75F, 4.01F, 4.50F},
            {4.85F, 4.01F, 18.50F},
        }},
    },
    {
        OldGuardPatrolRoute::ForecastlePort,
        {{
            {-2.70F, 4.51F, 26.00F},
            {-2.30F, 4.51F, 29.40F},
            {-0.85F, 4.51F, 34.20F},
            {-1.40F, 4.51F, 30.80F},
        }},
    },
    {
        OldGuardPatrolRoute::ForecastleStarboard,
        {{
            {2.70F, 4.51F, 26.00F},
            {2.30F, 4.51F, 29.40F},
            {0.85F, 4.51F, 34.20F},
            {1.40F, 4.51F, 30.80F},
        }},
    },
}};

auto finite_or(float value, float fallback) noexcept -> float {
    return std::isfinite(value) ? value : fallback;
}

auto finite_vec3_or(const glm::vec3& value, const glm::vec3& fallback) noexcept
    -> glm::vec3 {
    return std::isfinite(value.x) &&
                   std::isfinite(value.y) &&
                   std::isfinite(value.z)
               ? value
               : fallback;
}

auto finite_orientation_or_identity(const glm::quat& value) noexcept -> glm::quat {
    const auto length_squared =
        value.w * value.w +
        value.x * value.x +
        value.y * value.y +
        value.z * value.z;
    if (!std::isfinite(length_squared) || length_squared <= 1.0e-8F) {
        return {1.0F, 0.0F, 0.0F, 0.0F};
    }
    return glm::normalize(value);
}

auto normalized_angle(float value) noexcept -> float {
    return std::remainder(finite_or(value, 0.0F), kTwoPi);
}

auto rotate_towards(float current, float target, float maximum_delta) noexcept
    -> float {
    const auto delta = normalized_angle(target - current);
    return normalized_angle(
        current + std::clamp(delta, -maximum_delta, maximum_delta));
}

auto safe_direction(const glm::vec3& value, const glm::vec3& fallback) noexcept
    -> glm::vec3 {
    const auto finite_value = finite_vec3_or(value, fallback);
    const auto length_squared = glm::dot(finite_value, finite_value);
    if (!std::isfinite(length_squared) || length_squared <= 1.0e-8F) {
        return fallback;
    }
    return finite_value / std::sqrt(length_squared);
}

auto hash_u32(std::uint32_t value) noexcept -> std::uint32_t {
    value ^= value >> 16U;
    value *= 0x7FEB352DU;
    value ^= value >> 15U;
    value *= 0x846CA68BU;
    value ^= value >> 16U;
    return value;
}

auto random_unit(std::uint32_t seed) noexcept -> float {
    return static_cast<float>(hash_u32(seed) & 0x00FFFFFFU) /
           static_cast<float>(0x01000000U);
}

auto random_signed(std::uint32_t seed) noexcept -> float {
    return random_unit(seed) * 2.0F - 1.0F;
}

auto known_action(OldGuardAction action) noexcept -> bool {
    const auto value = static_cast<std::uint8_t>(action);
    return value <= static_cast<std::uint8_t>(OldGuardAction::Bayonet);
}

auto canonical_member(std::size_t index) noexcept -> OldGuardMemberSaveState {
    OldGuardMemberSaveState member {};
    member.id = static_cast<std::uint8_t>(index);
    member.route_index = static_cast<std::uint8_t>(index);
    member.route_step = 1U;
    member.local_position = kPatrols[index].points[0];
    const auto direction =
        kPatrols[index].points[1] -
        kPatrols[index].points[0];
    member.yaw_radians = normalized_angle(
        std::atan2(-direction.z, direction.x));
    member.action = OldGuardAction::Watch;
    member.musket_loaded = true;
    return member;
}

auto canonical_save_state() noexcept -> OldGuardSaveState {
    OldGuardSaveState state {};
    for (std::size_t index = 0; index < state.members.size(); ++index) {
        state.members[index] = canonical_member(index);
    }
    state.patrol_revision = kOldGuardPatrolRevision;
    state.initialized = true;
    return state;
}

auto watch_duration(std::uint8_t guard_id, std::uint8_t route_step) noexcept
    -> float {
    const auto seed =
        static_cast<std::uint32_t>(guard_id) * 0x9E3779B9U +
        static_cast<std::uint32_t>(route_step) * 0x85EBCA6BU;
    return 1.50F + random_unit(seed) * 1.50F;
}

auto world_point(const OldGuardPlatformFrame& frame,
                 const glm::vec3& local_point) noexcept -> glm::vec3 {
    return finite_vec3_or(frame.world_origin, glm::vec3 {0.0F}) +
           finite_orientation_or_identity(frame.orientation) *
               finite_vec3_or(local_point, glm::vec3 {0.0F});
}

auto world_direction(const OldGuardPlatformFrame& frame,
                     const glm::vec3& local_direction) noexcept -> glm::vec3 {
    return safe_direction(
        finite_orientation_or_identity(frame.orientation) * local_direction,
        {1.0F, 0.0F, 0.0F});
}

auto local_forward(float yaw) noexcept -> glm::vec3 {
    return {
        std::cos(yaw),
        0.0F,
        -std::sin(yaw),
    };
}

auto horizontal_distance(const glm::vec3& first,
                         const glm::vec3& second) noexcept -> float {
    const auto delta = second - first;
    return std::sqrt(delta.x * delta.x + delta.z * delta.z);
}

auto ray_sphere_distance(const glm::vec3& origin,
                         const glm::vec3& direction,
                         const glm::vec3& center,
                         float radius,
                         float maximum_distance) noexcept
    -> std::optional<float> {
    const auto to_center = center - origin;
    const auto projection = glm::dot(to_center, direction);
    const auto distance_squared =
        glm::dot(to_center, to_center) - projection * projection;
    const auto radius_squared = radius * radius;
    if (distance_squared > radius_squared) {
        return std::nullopt;
    }
    const auto half_chord =
        std::sqrt(std::max(radius_squared - distance_squared, 0.0F));
    auto distance = projection - half_chord;
    if (distance < 0.0F) {
        distance = projection + half_chord;
    }
    if (distance < 0.0F || distance > maximum_distance) {
        return std::nullopt;
    }
    return distance;
}

auto segment_blocked_by_sphere(const glm::vec3& origin,
                               const glm::vec3& direction,
                               float distance,
                               const glm::vec3& center,
                               float radius) noexcept -> bool {
    const auto hit = ray_sphere_distance(
        origin,
        direction,
        center,
        std::max(radius, 0.05F),
        distance);
    return hit.has_value() && *hit < distance - 0.06F;
}

auto action_progress(const OldGuardMemberSaveState& member) noexcept -> float {
    const auto duration = old_guard_action_duration(member.action);
    if (member.action == OldGuardAction::Reload) {
        return std::clamp(
            1.0F - member.reload_remaining / kOldGuardReloadSeconds,
            0.0F,
            1.0F);
    }
    if (duration <= 1.0e-5F) {
        return 0.0F;
    }
    return std::clamp(member.action_time / duration, 0.0F, 1.0F);
}

} // namespace

auto old_guard_patrols() noexcept
    -> const std::array<OldGuardPatrol, kOldGuardMemberCount>& {
    return kPatrols;
}

auto old_guard_is_hostile(const OldGuardTargetCandidate& target) noexcept
    -> bool {
    const auto transformed_animal =
        target.species == CreatureSpecies::Pig ||
        target.species == CreatureSpecies::Cow ||
        target.species == CreatureSpecies::Sheep;
    return transformed_animal &&
           target.phase == CreaturePhase::Night &&
           std::isfinite(target.morph_factor) &&
           target.morph_factor >= 0.999F &&
           std::isfinite(target.health) &&
           target.health > 0.0F &&
           std::isfinite(target.position.x) &&
           std::isfinite(target.position.y) &&
           std::isfinite(target.position.z) &&
           std::isfinite(target.aim_position.x) &&
           std::isfinite(target.aim_position.y) &&
           std::isfinite(target.aim_position.z);
}

auto old_guard_action_duration(OldGuardAction action) noexcept -> float {
    switch (action) {
    case OldGuardAction::RaiseMusket:
        return kOldGuardRaiseSeconds;
    case OldGuardAction::StabilizeAim:
        return kOldGuardAimSeconds;
    case OldGuardAction::Fire:
        return kOldGuardFirePoseSeconds;
    case OldGuardAction::Reload:
        return kOldGuardReloadSeconds;
    case OldGuardAction::Bayonet:
        return kOldGuardBayonetSeconds;
    case OldGuardAction::Patrol:
    case OldGuardAction::Watch:
    default:
        return 0.0F;
    }
}

auto sanitize_old_guard_save_state(const OldGuardSaveState& source) noexcept
    -> OldGuardSaveState {
    if (!source.initialized) {
        return canonical_save_state();
    }

    auto result = source;
    result.initialized = true;
    const auto revision_valid =
        result.patrol_revision == kOldGuardPatrolRevision;
    result.patrol_revision = kOldGuardPatrolRevision;

    for (std::size_t index = 0; index < result.members.size(); ++index) {
        auto& member = result.members[index];
        const auto canonical = canonical_member(index);
        const auto valid_reload =
            std::isfinite(member.reload_remaining) &&
            member.reload_remaining > 0.0F &&
            member.reload_remaining <= kOldGuardReloadSeconds;
        const auto preserved_reload =
            valid_reload ? member.reload_remaining : 0.0F;
        const auto preserved_loaded =
            member.musket_loaded || !valid_reload;
        const auto preserved_cooldown =
            std::clamp(
                finite_or(member.bayonet_cooldown, 0.0F),
                0.0F,
                kOldGuardBayonetCooldownSeconds);

        member.id = static_cast<std::uint8_t>(index);
        if (!revision_valid) {
            member = canonical;
            member.musket_loaded = preserved_loaded;
            member.reload_remaining =
                preserved_loaded ? 0.0F : preserved_reload;
            member.bayonet_cooldown = preserved_cooldown;
            if (!member.musket_loaded) {
                member.action = OldGuardAction::Reload;
                member.action_time =
                    kOldGuardReloadSeconds - member.reload_remaining;
            }
            continue;
        }

        member.route_index = static_cast<std::uint8_t>(index);
        if (member.route_step >= kOldGuardRoutePointCount) {
            member.route_step = canonical.route_step;
        }
        member.local_position = finite_vec3_or(
            member.local_position,
            canonical.local_position);
        member.local_position.x =
            std::clamp(member.local_position.x, -8.0F, 8.0F);
        member.local_position.y =
            std::clamp(member.local_position.y, 3.80F, 4.70F);
        member.local_position.z =
            std::clamp(member.local_position.z, -35.0F, 38.0F);
        member.yaw_radians = normalized_angle(member.yaw_radians);
        member.animation_time = std::fmod(
            std::max(finite_or(member.animation_time, 0.0F), 0.0F),
            4096.0F);
        member.action_time =
            std::max(finite_or(member.action_time, 0.0F), 0.0F);
        member.bayonet_cooldown = preserved_cooldown;
        if (!known_action(member.action)) {
            member.action = OldGuardAction::Watch;
            member.action_time = 0.0F;
        }

        if (member.musket_loaded) {
            member.reload_remaining = 0.0F;
            if (member.action == OldGuardAction::Reload ||
                member.action == OldGuardAction::Fire) {
                member.action = OldGuardAction::Watch;
                member.action_time = 0.0F;
            }
        } else if (!valid_reload) {
            member.musket_loaded = true;
            member.reload_remaining = 0.0F;
            member.action = OldGuardAction::Watch;
            member.action_time = 0.0F;
        } else {
            member.reload_remaining = preserved_reload;
            if (member.action == OldGuardAction::Reload) {
                member.action_time =
                    kOldGuardReloadSeconds - member.reload_remaining;
            } else if (member.action != OldGuardAction::Bayonet &&
                       member.action != OldGuardAction::Fire) {
                member.action = OldGuardAction::Reload;
                member.action_time =
                    kOldGuardReloadSeconds - member.reload_remaining;
            }
        }

        const auto duration = old_guard_action_duration(member.action);
        if (duration > 0.0F) {
            member.action_time =
                std::clamp(member.action_time, 0.0F, duration);
        }
    }
    return result;
}

void OldGuardSystem::initialize_canonical_roster() noexcept {
    state_ = canonical_save_state();
    runtime_ = {};
    for (auto& runtime : runtime_) {
        runtime.perception_accumulator = kOldGuardPerceptionInterval;
    }
}

void OldGuardSystem::reset(int world_seed) noexcept {
    initialize_canonical_roster();
    appearance_seed_ = hash_u32(static_cast<std::uint32_t>(world_seed) ^ 0x56494555U);
    event_sequence_ = 0U;
    last_platform_ = {};
    render_instances_ = {};
    events_.clear();
    clear_transient_effects();
}

void OldGuardSystem::load_state(const OldGuardSaveState& state,
                                int world_seed) noexcept {
    state_ = sanitize_old_guard_save_state(state);
    runtime_ = {};
    for (auto& runtime : runtime_) {
        runtime.perception_accumulator = kOldGuardPerceptionInterval;
    }
    appearance_seed_ = hash_u32(static_cast<std::uint32_t>(world_seed) ^ 0x56494555U);
    event_sequence_ = 0U;
    render_instances_ = {};
    events_.clear();
    clear_transient_effects();
}

auto OldGuardSystem::target_for(
    std::size_t member_index,
    std::span<const OldGuardTargetCandidate> targets) const noexcept
    -> const OldGuardTargetCandidate* {
    if (member_index >= runtime_.size() ||
        !runtime_[member_index].target_id.has_value()) {
        return nullptr;
    }
    const auto target_id = *runtime_[member_index].target_id;
    for (const auto& target : targets) {
        if (target.stable_id == target_id && old_guard_is_hostile(target)) {
            return &target;
        }
    }
    return nullptr;
}

void OldGuardSystem::refresh_perception(
    std::size_t member_index,
    const OldGuardUpdateContext& context) {
    auto& member = state_.members[member_index];
    auto& runtime = runtime_[member_index];
    runtime.target_visible = false;

    const auto guard_position =
        world_point(context.platform, member.local_position);
    const auto eye =
        guard_position +
        finite_orientation_or_identity(context.platform.orientation) *
            glm::vec3 {0.0F, 1.67F, 0.0F};
    const auto scan_offset =
        std::sin(
            runtime.scan_time * 0.72F +
            static_cast<float>(member.id) * 1.173F) *
        kOldGuardScanAmplitude;
    const auto scan_direction = world_direction(
        context.platform,
        local_forward(member.yaw_radians + scan_offset));
    const auto horizontal_scan = safe_direction(
        {scan_direction.x, 0.0F, scan_direction.z},
        {1.0F, 0.0F, 0.0F});
    const auto minimum_dot = std::cos(kOldGuardVisionHalfAngle);

    const OldGuardTargetCandidate* best = nullptr;
    auto best_distance = std::numeric_limits<float>::infinity();
    for (const auto& target : context.targets) {
        if (!old_guard_is_hostile(target)) {
            continue;
        }
        const auto to_target = target.aim_position - eye;
        const auto distance = glm::length(to_target);
        if (!std::isfinite(distance) ||
            distance > kOldGuardMusketRange + 1.0e-4F) {
            continue;
        }

        const auto melee_awareness =
            horizontal_distance(guard_position, target.position) <=
            kOldGuardBayonetRange;
        const auto horizontal_target = safe_direction(
            {to_target.x, 0.0F, to_target.z},
            horizontal_scan);
        if (!melee_awareness &&
            glm::dot(horizontal_scan, horizontal_target) < minimum_dot) {
            continue;
        }
        if (context.visibility_clear &&
            !context.visibility_clear(eye, target.aim_position)) {
            continue;
        }
        if (distance < best_distance) {
            best = &target;
            best_distance = distance;
        }
    }

    if (best != nullptr) {
        runtime.target_id = best->stable_id;
        runtime.last_seen_position = best->aim_position;
        runtime.target_memory = kOldGuardTargetMemorySeconds;
        runtime.target_visible = true;
        return;
    }

    if (target_for(member_index, context.targets) == nullptr) {
        runtime.target_id.reset();
        runtime.target_memory = 0.0F;
    }
}

auto OldGuardSystem::safe_line_of_fire(
    std::size_t member_index,
    const OldGuardUpdateContext& context,
    const OldGuardTargetCandidate& target,
    const glm::vec3& origin,
    const glm::vec3& direction,
    float distance) const -> bool {
    if (!std::isfinite(distance) ||
        distance < 0.0F ||
        distance > kOldGuardMusketRange + 1.0e-4F) {
        return false;
    }
    if (context.shot_clear &&
        !context.shot_clear(
            origin,
            direction,
            distance,
            target.stable_id)) {
        return false;
    }

    for (const auto& candidate : context.targets) {
        if (candidate.stable_id == target.stable_id) {
            continue;
        }
        if (segment_blocked_by_sphere(
                origin,
                direction,
                distance,
                candidate.aim_position,
                std::max(finite_or(candidate.body_radius, 0.5F), 0.10F))) {
            return false;
        }
    }

    for (const auto& occupant : context.occupants) {
        if (!occupant.blocks_shot) {
            continue;
        }
        // Je traite deja les six gardes avec leurs positions simulees exactes
        // juste apres cette boucle ; leur copie externe, figee pour la frame,
        // ne doit ni doubler un obstacle ni bloquer son propre deplacement.
        if (occupant.priority == OldGuardOccupantPriority::Guard) {
            continue;
        }
        if (segment_blocked_by_sphere(
                origin,
                direction,
                distance,
                occupant.position,
                std::max(finite_or(occupant.radius, 0.45F), 0.10F))) {
            return false;
        }
    }

    const auto platform_up =
        finite_orientation_or_identity(context.platform.orientation) *
        glm::vec3 {0.0F, 0.95F, 0.0F};
    for (std::size_t guard_index = 0;
         guard_index < state_.members.size();
         ++guard_index) {
        if (guard_index == member_index) {
            continue;
        }
        const auto center =
            world_point(
                context.platform,
                state_.members[guard_index].local_position) +
            platform_up;
        if (segment_blocked_by_sphere(
                origin,
                direction,
                distance,
                center,
                kOldGuardBodyRadius)) {
            return false;
        }
    }
    return true;
}

void OldGuardSystem::emit_shot(
    std::size_t member_index,
    const OldGuardUpdateContext& context,
    const OldGuardTargetCandidate& target) {
    auto& member = state_.members[member_index];
    member.action = OldGuardAction::Fire;
    member.action_time = 0.0F;
    member.musket_loaded = false;
    member.reload_remaining = kOldGuardReloadSeconds;
    rebuild_render_instances(context);

    // Je raffine deux fois la direction vers la cible depuis la vraie bouche :
    // le faible decalage entre l'oeil et le canon ne produit donc aucun tir
    // visuellement parallele qui manquerait sa cible a cinquante metres.
    auto& render = render_instances_[member_index];
    auto pose = sample_old_guard_pose(render);
    for (int iteration = 0; iteration < 2; ++iteration) {
        render.aim_direction = safe_direction(
            target.aim_position - pose.muzzle_position,
            pose.muzzle_forward);
        pose = sample_old_guard_pose(render);
    }
    runtime_[member_index].weapon_aim_direction =
        render.aim_direction;
    runtime_[member_index].weapon_aim_override = true;
    const auto distance = glm::length(
        target.aim_position - pose.muzzle_position);
    const auto direction = pose.muzzle_forward;
    if (!runtime_[member_index].target_visible ||
        !safe_line_of_fire(
            member_index,
            context,
            target,
            pose.muzzle_position,
            direction,
            distance)) {
        member.action = OldGuardAction::Watch;
        member.action_time = 0.0F;
        member.musket_loaded = true;
        member.reload_remaining = 0.0F;
        runtime_[member_index].weapon_aim_override = false;
        rebuild_render_instances(context);
        return;
    }

    OldGuardShotEvent event {};
    event.muzzle_position = pose.muzzle_position;
    event.direction = direction;
    event.target_position = target.aim_position;
    event.maximum_distance = kOldGuardMusketRange;
    event.damage = kOldGuardMusketDamage;
    event.target_id = target.stable_id;
    event.sequence = ++event_sequence_;
    event.guard_id = member.id;
    events_.shots.push_back(event);
    spawn_muzzle_effects(event, context);
    if (context.on_shot) {
        context.on_shot(event);
    }
}

void OldGuardSystem::emit_bayonet(
    std::size_t member_index,
    const OldGuardUpdateContext& context,
    const OldGuardTargetCandidate& target) {
    auto& member = state_.members[member_index];
    member.action_time = kOldGuardBayonetHitTime;
    rebuild_render_instances(context);
    const auto pose =
        sample_old_guard_pose(render_instances_[member_index]);
    const auto guard_position =
        world_point(context.platform, member.local_position);
    if (horizontal_distance(guard_position, target.position) >
            kOldGuardBayonetRange + 1.0e-4F ||
        std::abs(target.position.y - guard_position.y) > 1.60F ||
        (context.melee_clear &&
         !context.melee_clear(pose.bayonet_base, pose.bayonet_tip))) {
        return;
    }

    OldGuardBayonetEvent event {};
    event.origin = pose.bayonet_base;
    event.tip_position = pose.bayonet_tip;
    event.direction = safe_direction(
        pose.bayonet_tip - pose.bayonet_base,
        pose.muzzle_forward);
    event.target_id = target.stable_id;
    event.sequence = ++event_sequence_;
    event.guard_id = member.id;
    events_.bayonet_hits.push_back(event);
    if (context.on_bayonet) {
        context.on_bayonet(event);
    }
}

void OldGuardSystem::update_member(
    std::size_t member_index,
    const OldGuardUpdateContext& context,
    float dt) {
    auto& member = state_.members[member_index];
    auto& runtime = runtime_[member_index];
    auto remaining = dt;
    auto iterations = 0;

    const auto target_in_melee = [&]() {
        const auto* target = target_for(member_index, context.targets);
        if (target == nullptr) {
            return false;
        }
        const auto position =
            world_point(context.platform, member.local_position);
        return horizontal_distance(position, target->position) <=
                   kOldGuardBayonetRange &&
               std::abs(position.y - target->position.y) <= 1.60F;
    };

    while (remaining > 1.0e-6F && iterations < 64) {
        ++iterations;
        const auto* target = target_for(member_index, context.targets);
        if (target == nullptr) {
            runtime.target_id.reset();
            runtime.target_visible = false;
            runtime.target_memory = 0.0F;
        }

        if ((member.action == OldGuardAction::Patrol ||
             member.action == OldGuardAction::Watch) &&
            target != nullptr &&
            (runtime.target_visible || target_in_melee())) {
            if (member.musket_loaded) {
                member.action = OldGuardAction::RaiseMusket;
                member.action_time = 0.0F;
            } else if (target_in_melee() &&
                       member.bayonet_cooldown <= 0.0F) {
                member.action = OldGuardAction::Bayonet;
                member.action_time = 0.0F;
                runtime.bayonet_hit_emitted = false;
            } else {
                member.action = OldGuardAction::Reload;
                member.action_time =
                    kOldGuardReloadSeconds - member.reload_remaining;
            }
            continue;
        }

        switch (member.action) {
        case OldGuardAction::Patrol: {
            const auto route_index =
                std::min<std::size_t>(
                    member.route_index,
                    kOldGuardMemberCount - 1U);
            const auto step_index =
                std::min<std::size_t>(
                    member.route_step,
                    kOldGuardRoutePointCount - 1U);
            const auto destination =
                kPatrols[route_index].points[step_index];
            const auto delta = destination - member.local_position;
            const auto distance = glm::length(delta);
            if (!std::isfinite(distance) ||
                distance <= 1.0e-6F) {
                member.local_position = destination;
                member.route_step = static_cast<std::uint8_t>(
                    (step_index + 1U) % kOldGuardRoutePointCount);
                member.action = OldGuardAction::Watch;
                member.action_time = 0.0F;
                runtime.current_speed = 0.0F;
                continue;
            }

            const auto direction = delta / distance;
            const auto movement_distance =
                std::min(distance, kOldGuardWalkSpeed * remaining);
            const auto desired =
                member.local_position + direction * movement_distance;
            const auto desired_world =
                world_point(context.platform, desired);
            const auto platform_body_offset =
                finite_orientation_or_identity(
                    context.platform.orientation) *
                glm::vec3 {0.0F, 0.92F, 0.0F};
            const auto desired_center =
                desired_world + platform_body_offset;
            auto occupied = false;
            for (const auto& occupant : context.occupants) {
                if (occupant.priority ==
                    OldGuardOccupantPriority::Guard) {
                    continue;
                }
                const auto clearance =
                    kOldGuardBodyRadius +
                    std::max(finite_or(occupant.radius, 0.45F), 0.10F);
                if (glm::length(desired_center - occupant.position) <
                    clearance) {
                    occupied = true;
                    break;
                }
            }
            if (!occupied) {
                for (std::size_t other = 0;
                     other < state_.members.size();
                     ++other) {
                    if (other == member_index) continue;
                    const auto other_world =
                        world_point(
                            context.platform,
                            state_.members[other].local_position) +
                        platform_body_offset;
                    if (glm::length(desired_center - other_world) <
                        kOldGuardBodyRadius * 2.0F) {
                        occupied = true;
                        break;
                    }
                }
            }
            if (occupied) {
                runtime.current_speed = 0.0F;
                remaining = 0.0F;
                break;
            }

            const auto desired_yaw =
                std::atan2(-direction.z, direction.x);
            member.yaw_radians = rotate_towards(
                member.yaw_radians,
                desired_yaw,
                kOldGuardTurnSpeed * remaining);
            member.local_position = desired;
            runtime.locomotion_distance += movement_distance;
            runtime.current_speed = kOldGuardWalkSpeed;
            const auto consumed =
                movement_distance / kOldGuardWalkSpeed;
            remaining = std::max(remaining - consumed, 0.0F);
            if (movement_distance + 1.0e-6F >= distance) {
                member.local_position = destination;
                member.route_step = static_cast<std::uint8_t>(
                    (step_index + 1U) % kOldGuardRoutePointCount);
                member.action = OldGuardAction::Watch;
                member.action_time = 0.0F;
                runtime.current_speed = 0.0F;
            } else {
                remaining = 0.0F;
            }
            break;
        }
        case OldGuardAction::Watch: {
            runtime.current_speed = 0.0F;
            const auto duration =
                watch_duration(member.id, member.route_step);
            const auto step =
                std::min(remaining, std::max(duration - member.action_time, 0.0F));
            member.action_time += step;
            remaining -= step;
            if (member.action_time + 1.0e-6F >= duration) {
                member.action = OldGuardAction::Patrol;
                member.action_time = 0.0F;
            } else {
                remaining = 0.0F;
            }
            break;
        }
        case OldGuardAction::RaiseMusket:
        case OldGuardAction::StabilizeAim: {
            runtime.current_speed = 0.0F;
            if (target == nullptr || runtime.target_memory <= 0.0F) {
                member.action = OldGuardAction::Watch;
                member.action_time = 0.0F;
                continue;
            }
            const auto duration =
                old_guard_action_duration(member.action);
            const auto step =
                std::min(remaining, std::max(duration - member.action_time, 0.0F));
            member.action_time += step;
            remaining -= step;
            if (member.action_time + 1.0e-6F < duration) {
                remaining = 0.0F;
                break;
            }
            if (member.action == OldGuardAction::RaiseMusket) {
                member.action = OldGuardAction::StabilizeAim;
                member.action_time = 0.0F;
                continue;
            }
            if (!runtime.target_visible) {
                member.action = OldGuardAction::Watch;
                member.action_time = 0.0F;
                continue;
            }
            const auto shot_count = events_.shots.size();
            emit_shot(member_index, context, *target);
            if (events_.shots.size() == shot_count) {
                if (target_in_melee() &&
                    member.bayonet_cooldown <= 0.0F) {
                    member.action = OldGuardAction::Bayonet;
                    runtime.bayonet_hit_emitted = false;
                } else {
                    member.action = OldGuardAction::Watch;
                }
                member.action_time = 0.0F;
            }
            break;
        }
        case OldGuardAction::Fire: {
            runtime.current_speed = 0.0F;
            const auto step =
                std::min(
                    remaining,
                    std::max(
                        kOldGuardFirePoseSeconds - member.action_time,
                        0.0F));
            member.action_time += step;
            member.reload_remaining =
                std::max(member.reload_remaining - step, 0.0F);
            remaining -= step;
            if (member.action_time + 1.0e-6F >=
                kOldGuardFirePoseSeconds) {
                runtime.weapon_aim_override = false;
                member.action = OldGuardAction::Reload;
                member.action_time =
                    kOldGuardReloadSeconds - member.reload_remaining;
            } else {
                remaining = 0.0F;
            }
            break;
        }
        case OldGuardAction::Reload: {
            runtime.current_speed = 0.0F;
            if (target != nullptr &&
                target_in_melee() &&
                member.bayonet_cooldown <= 0.0F) {
                runtime.paused_reload_remaining =
                    member.reload_remaining;
                runtime.bayonet_hit_emitted = false;
                member.action = OldGuardAction::Bayonet;
                member.action_time = 0.0F;
                continue;
            }

            const auto step =
                std::min(remaining, member.reload_remaining);
            member.reload_remaining =
                std::max(member.reload_remaining - step, 0.0F);
            member.action_time =
                kOldGuardReloadSeconds - member.reload_remaining;
            remaining -= step;
            if (member.reload_remaining > 1.0e-6F) {
                remaining = 0.0F;
                break;
            }

            member.musket_loaded = true;
            member.reload_remaining = 0.0F;
            if (target != nullptr && runtime.target_visible) {
                const auto shot_count = events_.shots.size();
                emit_shot(member_index, context, *target);
                if (events_.shots.size() != shot_count) {
                    continue;
                }
                if (target_in_melee() &&
                    member.bayonet_cooldown <= 0.0F) {
                    member.action = OldGuardAction::Bayonet;
                    member.action_time = 0.0F;
                    runtime.bayonet_hit_emitted = false;
                    continue;
                }
            }
            member.action = OldGuardAction::Watch;
            member.action_time = 0.0F;
            break;
        }
        case OldGuardAction::Bayonet: {
            runtime.current_speed = 0.0F;
            const auto next_hit_time =
                runtime.bayonet_hit_emitted
                    ? kOldGuardBayonetSeconds
                    : kOldGuardBayonetHitTime;
            const auto boundary =
                std::min(next_hit_time, kOldGuardBayonetSeconds);
            const auto step =
                std::min(
                    remaining,
                    std::max(boundary - member.action_time, 0.0F));
            member.action_time += step;
            remaining -= step;

            if (!runtime.bayonet_hit_emitted &&
                member.action_time + 1.0e-6F >=
                    kOldGuardBayonetHitTime) {
                runtime.bayonet_hit_emitted = true;
                if (target != nullptr) {
                    emit_bayonet(member_index, context, *target);
                }
                member.action_time = kOldGuardBayonetHitTime;
                continue;
            }
            if (member.action_time + 1.0e-6F >=
                kOldGuardBayonetSeconds) {
                member.bayonet_cooldown =
                    kOldGuardBayonetCooldownSeconds;
                runtime.bayonet_hit_emitted = false;
                if (!member.musket_loaded &&
                    member.reload_remaining > 0.0F) {
                    member.action = OldGuardAction::Reload;
                    member.action_time =
                        kOldGuardReloadSeconds -
                        member.reload_remaining;
                } else {
                    member.action = OldGuardAction::Watch;
                    member.action_time = 0.0F;
                }
            } else {
                remaining = 0.0F;
            }
            break;
        }
        }
    }
}

void OldGuardSystem::rebuild_render_instances(
    const OldGuardUpdateContext& context) noexcept {
    const auto orientation =
        finite_orientation_or_identity(context.platform.orientation);
    for (std::size_t index = 0; index < state_.members.size(); ++index) {
        const auto& member = state_.members[index];
        const auto& runtime = runtime_[index];
        auto& render = render_instances_[index];
        render.position =
            world_point(context.platform, member.local_position);
        render.local_position = member.local_position;
        render.platform_orientation = orientation;
        render.yaw_radians = member.yaw_radians;
        render.animation_time = member.animation_time;
        render.action_time = member.action_time;
        render.action_progress = action_progress(member);
        render.reload_remaining = member.reload_remaining;
        render.locomotion_phase =
            runtime.locomotion_distance /
            kCrewLocomotionCycleDistance;
        render.motion_amount = std::clamp(
            runtime.current_speed / kOldGuardWalkSpeed,
            0.0F,
            1.0F);
        render.sky_light =
            std::clamp(finite_or(context.sky_light, 1.0F), 0.0F, 1.0F);
        render.local_light =
            std::clamp(finite_or(context.local_light, 0.0F), 0.0F, 1.0F);
        render.precipitation_exposure =
            std::clamp(
                finite_or(context.precipitation_exposure, 1.0F),
                0.0F,
                1.0F);
        render.appearance_seed =
            hash_u32(
                appearance_seed_ ^
                (static_cast<std::uint32_t>(member.id) + 1U) *
                    0x9E3779B9U);
        render.id = member.id;
        render.action = member.action;
        render.musket_loaded = member.musket_loaded;

        auto aim_position = runtime.last_seen_position;
        if (const auto* target = target_for(index, context.targets);
            target != nullptr) {
            aim_position = target->aim_position;
        }
        const auto default_aim = world_direction(
            context.platform,
            local_forward(
                member.yaw_radians +
                std::sin(
                    runtime.scan_time * 0.72F +
                    static_cast<float>(member.id) * 1.173F) *
                    kOldGuardScanAmplitude));
        render.aim_direction =
            runtime.weapon_aim_override &&
                    member.action == OldGuardAction::Fire
                ? safe_direction(
                      runtime.weapon_aim_direction,
                      default_aim)
                : runtime.target_id.has_value()
                ? safe_direction(
                      aim_position -
                          (render.position +
                           orientation *
                               glm::vec3 {0.0F, 1.55F, 0.0F}),
                      default_aim)
                : default_aim;
    }
}

auto OldGuardSystem::update(const OldGuardUpdateContext& context,
                            float dt) -> const OldGuardFrameEvents& {
    events_.clear();
    if (!state_.initialized) {
        initialize_canonical_roster();
    }

    const auto safe_dt =
        std::clamp(
            finite_or(dt, 0.0F),
            0.0F,
            kOldGuardMaximumSimulationStep);
    last_platform_.world_origin =
        finite_vec3_or(context.platform.world_origin, glm::vec3 {0.0F});
    last_platform_.velocity =
        finite_vec3_or(context.platform.velocity, glm::vec3 {0.0F});
    last_platform_.orientation =
        finite_orientation_or_identity(context.platform.orientation);
    rebuild_render_instances(context);

    auto remaining = safe_dt;
    while (true) {
        // Je traite la frontiere avant le mouvement suivant : le premier appel
        // observe donc immediatement le pont, puis exactement toutes les 0,10 s.
        for (std::size_t index = 0; index < state_.members.size(); ++index) {
            auto& runtime = runtime_[index];
            if (runtime.perception_accumulator + 1.0e-6F >=
                kOldGuardPerceptionInterval) {
                runtime.perception_accumulator = 0.0F;
                refresh_perception(index, context);
            }
        }
        if (remaining <= 1.0e-6F) {
            break;
        }

        auto step = remaining;
        for (std::size_t index = 0; index < state_.members.size(); ++index) {
            const auto& member = state_.members[index];
            const auto& runtime = runtime_[index];
            step = std::min(
                step,
                std::max(
                    kOldGuardPerceptionInterval -
                        runtime.perception_accumulator,
                    0.0F));
            if (member.bayonet_cooldown > 1.0e-6F) {
                step = std::min(step, member.bayonet_cooldown);
            }
            if (!runtime.target_visible &&
                runtime.target_memory > 1.0e-6F) {
                step = std::min(step, runtime.target_memory);
            }
        }
        if (step <= 1.0e-6F) {
            // Je force uniquement l'accumulateur arrondi a sa frontiere ; sans
            // cela une erreur flottante pourrait figer toute la simulation.
            for (auto& runtime : runtime_) {
                if (kOldGuardPerceptionInterval -
                        runtime.perception_accumulator <=
                    1.0e-6F) {
                    runtime.perception_accumulator =
                        kOldGuardPerceptionInterval;
                }
            }
            continue;
        }

        std::array<float, kOldGuardMemberCount> cooldown_before {};
        for (std::size_t index = 0; index < state_.members.size(); ++index) {
            cooldown_before[index] =
                state_.members[index].bayonet_cooldown;
            update_member(index, context, step);
        }

        for (std::size_t index = 0; index < state_.members.size(); ++index) {
            auto& member = state_.members[index];
            auto& runtime = runtime_[index];
            runtime.scan_time =
                std::fmod(runtime.scan_time + step, 4096.0F);
            runtime.perception_accumulator =
                std::min(
                    runtime.perception_accumulator + step,
                    kOldGuardPerceptionInterval);

            // Je ne retranche pas retroactivement le temps anterieur a la fin
            // d'un estoc : un cooldown cree dans ce sous-pas repart bien de 1 s.
            if (cooldown_before[index] > 0.0F) {
                member.bayonet_cooldown =
                    std::max(cooldown_before[index] - step, 0.0F);
            }
            if (!runtime.target_visible) {
                runtime.target_memory =
                    std::max(runtime.target_memory - step, 0.0F);
                if (runtime.target_memory <= 0.0F) {
                    runtime.target_id.reset();
                }
            }
            member.animation_time =
                std::fmod(member.animation_time + step, 4096.0F);
        }
        remaining = std::max(remaining - step, 0.0F);
    }
    rebuild_render_instances(context);
    return events_;
}

void OldGuardSystem::spawn_muzzle_effects(
    const OldGuardShotEvent& shot,
    const OldGuardUpdateContext& context) noexcept {
    const auto base_seed =
        hash_u32(
            static_cast<std::uint32_t>(shot.sequence) ^
            (static_cast<std::uint32_t>(shot.guard_id) + 1U) *
                0x9E3779B9U);
    OldGuardMuzzleFlashInstance flash {};
    flash.position = shot.muzzle_position;
    flash.direction = shot.direction;
    flash.lifetime = 0.05F + random_unit(base_seed + 1U) * 0.03F;
    flash.size = 0.28F + random_unit(base_seed + 2U) * 0.16F;
    flash.intensity = 0.90F + random_unit(base_seed + 3U) * 0.10F;
    flash.seed = base_seed;
    if (flash_count_ < flashes_.size()) {
        flashes_[flash_count_++] = flash;
    } else {
        const auto oldest = static_cast<std::size_t>(
            std::max_element(
                flashes_.begin(),
                flashes_.end(),
                [](const auto& first, const auto& second) {
                    return first.age < second.age;
                }) -
            flashes_.begin());
        flashes_[oldest] = flash;
    }

    const auto smoke_count =
        10U +
        static_cast<std::size_t>(hash_u32(base_seed + 4U) % 9U);
    const auto ship_velocity =
        finite_vec3_or(context.platform.velocity, glm::vec3 {0.0F});
    const auto wind =
        finite_vec3_or(context.wind_velocity, glm::vec3 {0.0F});
    for (std::size_t index = 0; index < smoke_count; ++index) {
        const auto particle_seed =
            hash_u32(
                base_seed +
                static_cast<std::uint32_t>(index) *
                    0x85EBCA6BU);
        OldGuardSmokeInstance puff {};
        const auto lateral = glm::vec3 {
            random_signed(particle_seed + 1U),
            random_signed(particle_seed + 2U),
            random_signed(particle_seed + 3U),
        };
        puff.position =
            shot.muzzle_position +
            lateral * (0.025F + random_unit(particle_seed + 4U) * 0.07F);
        puff.velocity =
            ship_velocity +
            wind * 0.18F +
            shot.direction *
                (0.28F + random_unit(particle_seed + 5U) * 0.65F) +
            lateral * 0.22F +
            glm::vec3 {0.0F, 0.16F, 0.0F};
        puff.lifetime =
            1.20F + random_unit(particle_seed + 6U) * 1.40F;
        puff.size =
            0.10F + random_unit(particle_seed + 7U) * 0.13F;
        puff.rotation_radians =
            random_unit(particle_seed + 8U) * kTwoPi;
        puff.angular_velocity =
            random_signed(particle_seed + 9U) * 1.8F;
        puff.opacity =
            0.68F + random_unit(particle_seed + 10U) * 0.25F;
        puff.seed = particle_seed;
        if (smoke_count_ < smoke_.size()) {
            smoke_[smoke_count_++] = puff;
        } else {
            const auto oldest = static_cast<std::size_t>(
                std::max_element(
                    smoke_.begin(),
                    smoke_.end(),
                    [](const auto& first, const auto& second) {
                        return first.age / first.lifetime <
                               second.age / second.lifetime;
                    }) -
                smoke_.begin());
            smoke_[oldest] = puff;
        }
    }
}

void OldGuardSystem::update_effects(float dt,
                                    const glm::vec3& wind_velocity) noexcept {
    const auto safe_dt =
        std::clamp(
            finite_or(dt, 0.0F),
            0.0F,
            kOldGuardMaximumSimulationStep);
    const auto wind =
        finite_vec3_or(wind_velocity, glm::vec3 {0.0F});

    auto flash_write = std::size_t {0U};
    for (std::size_t index = 0; index < flash_count_; ++index) {
        auto flash = flashes_[index];
        flash.age += safe_dt;
        if (flash.age < flash.lifetime) {
            flashes_[flash_write++] = flash;
        }
    }
    flash_count_ = flash_write;

    auto smoke_write = std::size_t {0U};
    for (std::size_t index = 0; index < smoke_count_; ++index) {
        auto puff = smoke_[index];
        puff.age += safe_dt;
        if (puff.age >= puff.lifetime) {
            continue;
        }
        puff.velocity += glm::vec3 {0.0F, 0.18F, 0.0F} * safe_dt;
        puff.velocity +=
            (wind - puff.velocity * 0.10F) *
            std::min(safe_dt * 0.42F, 1.0F);
        puff.position += puff.velocity * safe_dt;
        puff.rotation_radians = std::remainder(
            puff.rotation_radians +
                puff.angular_velocity * safe_dt,
            kTwoPi);
        const auto life_ratio =
            std::clamp(puff.age / puff.lifetime, 0.0F, 1.0F);
        puff.size += safe_dt * (0.12F + life_ratio * 0.18F);
        puff.opacity =
            std::clamp(
                (1.0F - life_ratio) *
                    (life_ratio < 0.12F
                         ? life_ratio / 0.12F
                         : 1.0F),
                0.0F,
                1.0F);
        smoke_[smoke_write++] = puff;
    }
    smoke_count_ = smoke_write;
}

void OldGuardSystem::clear_transient_effects() noexcept {
    flash_count_ = 0U;
    smoke_count_ = 0U;
    flashes_ = {};
    smoke_ = {};
}

auto OldGuardSystem::save_state() const noexcept
    -> const OldGuardSaveState& {
    return state_;
}

auto OldGuardSystem::members() const noexcept
    -> std::span<const OldGuardMemberSaveState> {
    return state_.members;
}

auto OldGuardSystem::render_instances() const noexcept
    -> std::span<const OldGuardRenderInstance> {
    return render_instances_;
}

auto OldGuardSystem::flashes() const noexcept
    -> std::span<const OldGuardMuzzleFlashInstance> {
    return {flashes_.data(), flash_count_};
}

auto OldGuardSystem::smoke() const noexcept
    -> std::span<const OldGuardSmokeInstance> {
    return {smoke_.data(), smoke_count_};
}

auto OldGuardSystem::last_events() const noexcept
    -> const OldGuardFrameEvents& {
    return events_;
}

auto OldGuardSystem::intercept_ray(
    const glm::vec3& origin,
    const glm::vec3& direction,
    float maximum_distance) const noexcept -> OldGuardRayHit {
    OldGuardRayHit result {};
    const auto safe_maximum =
        std::max(finite_or(maximum_distance, 0.0F), 0.0F);
    const auto normalized_direction =
        safe_direction(direction, {0.0F, 0.0F, -1.0F});
    auto nearest = safe_maximum;
    const auto up =
        finite_orientation_or_identity(last_platform_.orientation) *
        glm::vec3 {0.0F, 1.0F, 0.0F};
    for (const auto& guard : render_instances_) {
        const auto centers = std::array<glm::vec3, 3> {{
            guard.position + up * 0.48F,
            guard.position + up * 1.05F,
            guard.position + up * 1.66F,
        }};
        const auto radii = std::array<float, 3> {{0.31F, 0.42F, 0.25F}};
        for (std::size_t index = 0; index < centers.size(); ++index) {
            const auto hit = ray_sphere_distance(
                origin,
                normalized_direction,
                centers[index],
                radii[index],
                nearest);
            if (hit.has_value() && *hit <= nearest) {
                nearest = *hit;
                result.hit = true;
                result.distance = *hit;
                result.position =
                    origin + normalized_direction * *hit;
                result.guard_id = guard.id;
            }
        }
    }
    return result;
}

auto OldGuardSystem::focus_from_ray(
    const glm::vec3& origin,
    const glm::vec3& direction,
    float maximum_distance) const noexcept -> OldGuardFocusState {
    const auto hit =
        intercept_ray(origin, direction, maximum_distance);
    return {
        hit.hit,
        hit.distance,
        hit.guard_id,
    };
}

} // namespace valcraft
