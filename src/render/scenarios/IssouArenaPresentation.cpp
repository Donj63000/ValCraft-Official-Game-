#include "render/scenarios/IssouArenaPresentation.h"

#include <glm/common.hpp>
#include <glm/ext/matrix_transform.hpp>
#include <glm/geometric.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>

namespace valcraft {

namespace {

constexpr float kPi = 3.14159265358979323846F;

[[nodiscard]] auto finite_clamped(float value,
                                  float minimum,
                                  float maximum,
                                  float fallback = 0.0F) noexcept -> float {
    return std::isfinite(value)
        ? std::clamp(value, minimum, maximum)
        : fallback;
}

[[nodiscard]] auto next_random(std::uint32_t& state) noexcept
    -> std::uint32_t {
    state = state * 1664525U + 1013904223U;
    return state;
}

[[nodiscard]] auto next_unit(std::uint32_t& state) noexcept -> float {
    return static_cast<float>((next_random(state) >> 8U) & 0x00FFFFFFU) /
           static_cast<float>(0x01000000U);
}

[[nodiscard]] auto normalized_phase(float value) noexcept -> float {
    if (!std::isfinite(value)) {
        return 0.0F;
    }
    auto phase = std::fmod(value, 1.0F);
    if (phase < 0.0F) {
        phase += 1.0F;
    }
    return phase;
}

[[nodiscard]] auto reaction_for_event(
    IssouArenaEventKind event,
    float excitement,
    float selector) noexcept -> IssouCrowdReaction {
    switch (event) {
    case IssouArenaEventKind::CrowdApplause:
    case IssouArenaEventKind::CrowdCheer:
    case IssouArenaEventKind::Victory:
        return selector < excitement
            ? IssouCrowdReaction::Cheer
            : IssouCrowdReaction::Murmur;
    case IssouArenaEventKind::CrowdRoar:
    case IssouArenaEventKind::ChainsBroken:
        return IssouCrowdReaction::Roar;
    case IssouArenaEventKind::CrowdBoo:
    case IssouArenaEventKind::Defeat:
        return selector < 0.72F
            ? IssouCrowdReaction::Boo
            : IssouCrowdReaction::Silence;
    case IssouArenaEventKind::BriefSilence:
    case IssouArenaEventKind::CrowdSilence:
        return IssouCrowdReaction::Silence;
    case IssouArenaEventKind::ChainCrack:
    case IssouArenaEventKind::ColossusRoar:
        return selector < 0.64F
            ? IssouCrowdReaction::Flinch
            : IssouCrowdReaction::Murmur;
    default:
        return excitement > 0.58F && selector < excitement * 0.45F
            ? IssouCrowdReaction::Cheer
            : IssouCrowdReaction::Murmur;
    }
}

[[nodiscard]] auto reaction_scale(
    IssouCrowdReaction reaction) noexcept -> float {
    switch (reaction) {
    case IssouCrowdReaction::Idle:
        return 0.08F;
    case IssouCrowdReaction::Murmur:
        return 0.28F;
    case IssouCrowdReaction::Cheer:
        return 0.72F;
    case IssouCrowdReaction::Roar:
        return 1.0F;
    case IssouCrowdReaction::Flinch:
        return 0.64F;
    case IssouCrowdReaction::Boo:
        return 0.62F;
    case IssouCrowdReaction::Silence:
        return 0.0F;
    }
    return 0.0F;
}

[[nodiscard]] auto crowd_lod(float distance) noexcept
    -> IssouCrowdLod {
    if (distance < 28.0F) {
        return IssouCrowdLod::Full;
    }
    if (distance < 48.0F) {
        return IssouCrowdLod::Simplified;
    }
    if (distance < 72.0F) {
        return IssouCrowdLod::Impostor;
    }
    return IssouCrowdLod::Culled;
}

[[nodiscard]] auto crowd_color(std::uint8_t variant) noexcept
    -> glm::vec4 {
    constexpr std::array<glm::vec4, 16U> palette {{
        {0.16F, 0.20F, 0.30F, 1.0F},
        {0.42F, 0.09F, 0.10F, 1.0F},
        {0.68F, 0.42F, 0.12F, 1.0F},
        {0.19F, 0.34F, 0.25F, 1.0F},
        {0.46F, 0.31F, 0.22F, 1.0F},
        {0.22F, 0.16F, 0.28F, 1.0F},
        {0.63F, 0.56F, 0.38F, 1.0F},
        {0.12F, 0.31F, 0.37F, 1.0F},
        {0.55F, 0.16F, 0.20F, 1.0F},
        {0.28F, 0.35F, 0.15F, 1.0F},
        {0.31F, 0.20F, 0.12F, 1.0F},
        {0.36F, 0.29F, 0.43F, 1.0F},
        {0.62F, 0.32F, 0.09F, 1.0F},
        {0.15F, 0.25F, 0.18F, 1.0F},
        {0.43F, 0.43F, 0.46F, 1.0F},
        {0.50F, 0.18F, 0.32F, 1.0F},
    }};
    return palette[static_cast<std::size_t>(variant) % palette.size()];
}

[[nodiscard]] auto uniform_uvs(
    CreatureAtlasTile tile) noexcept -> std::array<BoxUvRect, 6U> {
    const auto coordinates = creature_atlas_tile_coordinates(tile);
    constexpr auto step = 1.0F / kCreatureAtlasTilesPerAxis;
    const auto rect = BoxUvRect {
        static_cast<float>(coordinates[0]) * step,
        static_cast<float>(coordinates[1]) * step,
        static_cast<float>(coordinates[0] + 1) * step,
        static_cast<float>(coordinates[1] + 1) * step,
    };
    auto result = std::array<BoxUvRect, 6U> {};
    result.fill(rect);
    return result;
}

void append_crowd_box(std::vector<CreaturePartInstance>& parts,
                      const glm::mat4& root,
                      const glm::vec3& center,
                      const glm::vec3& dimensions,
                      const glm::vec3& rotation,
                      CreatureAtlasTile tile,
                      float material_class) {
    auto transform = glm::translate(root, center);
    transform =
        glm::rotate(transform, rotation.z, glm::vec3 {0.0F, 0.0F, 1.0F});
    transform =
        glm::rotate(transform, rotation.x, glm::vec3 {1.0F, 0.0F, 0.0F});
    transform = glm::scale(transform, dimensions);
    parts.push_back({
        transform,
        uniform_uvs(tile),
        0.0F,
        0.0F,
        material_class,
        0.22F,
        0.0F,
        1.0F,
        0.0F,
        1.0F,
    });
}

[[nodiscard]] auto safe_viewport(float value, float fallback) noexcept
    -> float {
    return std::isfinite(value) && value >= 320.0F
        ? value
        : fallback;
}

} // namespace

auto build_issou_crowd(
    const IssouArenaLayout& layout,
    const IssouCrowdRequest& raw_request)
    -> std::vector<IssouCrowdInstance> {
    auto request = raw_request;
    request.desired_count = std::clamp(
        request.desired_count,
        kIssouMinimumCrowdSize,
        kIssouMaximumCrowdSize);
    request.variant_count = std::clamp(
        request.variant_count,
        kIssouMinimumCrowdVariants,
        kIssouMaximumCrowdVariants);
    request.excitement =
        finite_clamped(request.excitement, 0.0F, 1.0F);
    const auto time = std::isfinite(request.animation_seconds)
        ? request.animation_seconds
        : 0.0F;
    auto seed = request.seed == 0U ? 1U : request.seed;

    const auto center_x =
        (static_cast<float>(layout.combat_bounds.min_x) +
         static_cast<float>(layout.combat_bounds.max_x)) *
        0.5F;
    const auto center_z =
        (static_cast<float>(layout.combat_bounds.min_z) +
         static_cast<float>(layout.combat_bounds.max_z)) *
        0.5F;
    const auto half_x =
        static_cast<float>(
            layout.combat_bounds.max_x -
            layout.combat_bounds.min_x + 1) *
        0.5F;
    const auto half_z =
        static_cast<float>(
            layout.combat_bounds.max_z -
            layout.combat_bounds.min_z + 1) *
        0.5F;

    std::vector<IssouCrowdInstance> crowd {};
    crowd.reserve(request.desired_count);
    for (std::size_t index = 0U;
         index < request.desired_count;
         ++index) {
        const auto side = index % 4U;
        const auto tier = (index / 4U) % 4U;
        const auto row_index = index / 16U;
        const auto row_count =
            std::max<std::size_t>(1U, (request.desired_count + 15U) / 16U);
        const auto row_t =
            (static_cast<float>(row_index) + 0.5F) /
            static_cast<float>(row_count);
        const auto jitter = (next_unit(seed) - 0.5F) * 0.56F;
        const auto height =
            static_cast<float>(layout.floor_y) + 1.75F +
            static_cast<float>(tier) * 0.78F;

        auto position = glm::vec3 {center_x, height, center_z};
        auto yaw = 0.0F;
        if (side == 0U || side == 1U) {
            position.x =
                center_x + (row_t * 2.0F - 1.0F) * (half_x - 1.0F) +
                jitter;
            position.z =
                center_z +
                (side == 0U ? -1.0F : 1.0F) *
                    (half_z + 1.2F + static_cast<float>(tier) * 0.72F);
            yaw = side == 0U ? 0.0F : kPi;
        } else {
            position.z =
                center_z + (row_t * 2.0F - 1.0F) * (half_z - 1.0F) +
                jitter;
            position.x =
                center_x +
                (side == 2U ? -1.0F : 1.0F) *
                    (half_x + 1.2F + static_cast<float>(tier) * 0.72F);
            yaw = side == 2U ? -kPi * 0.5F : kPi * 0.5F;
        }

        const auto selector = next_unit(seed);
        const auto reaction = reaction_for_event(
            request.latest_event, request.excitement, selector);
        const auto motion_scale =
            request.reduced_motion ? 0.18F : 1.0F;
        const auto reaction_amount =
            reaction_scale(reaction) *
            (0.72F + next_unit(seed) * 0.28F) *
            motion_scale;
        const auto variant =
            static_cast<std::uint8_t>(
                index < request.variant_count
                    ? index
                    : next_random(seed) % request.variant_count);
        const auto distance =
            glm::length(position - request.camera_position);
        crowd.push_back({
            position,
            yaw,
            normalized_phase(
                time * (0.56F + next_unit(seed) * 0.38F) +
                    next_unit(seed)),
            reaction_amount,
            crowd_color(variant),
            reaction,
            crowd_lod(distance),
            static_cast<std::uint16_t>(index),
            variant,
        });
    }
    return crowd;
}

auto build_issou_crowd_member_parts(
    const IssouCrowdInstance& instance)
    -> std::vector<CreaturePartInstance> {
    if (instance.lod == IssouCrowdLod::Culled) {
        return {};
    }
    auto root = glm::translate(glm::mat4 {1.0F}, instance.position);
    root = glm::rotate(
        root, instance.yaw_radians,
        glm::vec3 {0.0F, 1.0F, 0.0F});
    const auto phase = finite_clamped(
        instance.animation_phase, 0.0F, 1.0F);
    const auto amount = finite_clamped(
        instance.reaction_amount, 0.0F, 1.0F);
    const auto bounce =
        std::sin(phase * kPi * 2.0F) * 0.06F * amount;
    const auto arm = amount * (0.18F + 0.72F * std::abs(
        std::sin(phase * kPi * 2.0F)));
    const auto cloth_tile =
        static_cast<std::uint8_t>(instance.variant) % 2U == 0U
            ? CreatureAtlasTile::CrewNavyCloth
            : CreatureAtlasTile::CrewBurgundyCloth;

    std::vector<CreaturePartInstance> parts {};
    parts.reserve(instance.lod == IssouCrowdLod::Full ? 6U : 3U);
    append_crowd_box(
        parts, root, {0.0F, 0.78F + bounce, 0.0F},
        {0.42F, 0.78F, 0.28F}, {}, cloth_tile, 0.40F);
    append_crowd_box(
        parts, root, {0.0F, 1.40F + bounce, 0.0F},
        {0.30F, 0.34F, 0.28F}, {},
        CreatureAtlasTile::CrewSkinMedium, 0.34F);
    if (instance.lod == IssouCrowdLod::Full) {
        append_crowd_box(
            parts, root, {-0.29F, 0.98F + bounce, 0.0F},
            {0.14F, 0.62F, 0.14F}, {0.0F, 0.0F, arm},
            CreatureAtlasTile::CrewSkinMedium, 0.34F);
        append_crowd_box(
            parts, root, {0.29F, 0.98F + bounce, 0.0F},
            {0.14F, 0.62F, 0.14F}, {0.0F, 0.0F, -arm},
            CreatureAtlasTile::CrewSkinMedium, 0.34F);
        append_crowd_box(
            parts, root, {-0.12F, 0.25F, 0.0F},
            {0.15F, 0.50F, 0.17F}, {},
            CreatureAtlasTile::CrewLeather, 0.52F);
        append_crowd_box(
            parts, root, {0.12F, 0.25F, 0.0F},
            {0.15F, 0.50F, 0.17F}, {},
            CreatureAtlasTile::CrewLeather, 0.52F);
    } else {
        append_crowd_box(
            parts, root, {0.0F, 0.25F, 0.0F},
            {0.28F, 0.50F, 0.18F}, {},
            CreatureAtlasTile::CrewLeather, 0.52F);
    }
    return parts;
}

auto build_issou_arena_decor(
    const IssouArenaState& state)
    -> std::vector<IssouArenaDecorInstance> {
    std::vector<IssouArenaDecorInstance> decor {};
    decor.reserve(
        state.layout.braziers.size() +
        state.layout.gate_cells.size() +
        state.layout.chain_anchors.size() * 12U + 8U);

    const auto center_x =
        (static_cast<float>(state.layout.combat_bounds.min_x) +
         static_cast<float>(state.layout.combat_bounds.max_x)) *
        0.5F;
    const auto center_z =
        (static_cast<float>(state.layout.combat_bounds.min_z) +
         static_cast<float>(state.layout.combat_bounds.max_z)) *
        0.5F;
    const auto half_x =
        static_cast<float>(
            state.layout.combat_bounds.max_x -
            state.layout.combat_bounds.min_x + 1) *
        0.5F;
    const auto half_z =
        static_cast<float>(
            state.layout.combat_bounds.max_z -
            state.layout.combat_bounds.min_z + 1) *
        0.5F;
    for (std::size_t side = 0U; side < 4U; ++side) {
        auto stand_position = glm::vec3 {
            center_x,
            static_cast<float>(state.layout.floor_y) + 1.0F,
            center_z,
        };
        auto stand_dimensions = glm::vec3 {1.0F};
        if (side < 2U) {
            stand_position.z +=
                (side == 0U ? -1.0F : 1.0F) * (half_z + 3.0F);
            stand_dimensions = {half_x * 2.0F, 2.0F, 5.0F};
        } else {
            stand_position.x +=
                (side == 2U ? -1.0F : 1.0F) * (half_x + 3.0F);
            stand_dimensions = {5.0F, 2.0F, half_z * 2.0F};
        }
        auto stand_transform =
            glm::translate(glm::mat4 {1.0F}, stand_position);
        stand_transform = glm::scale(stand_transform, stand_dimensions);
        decor.push_back({
            stand_transform, IssouArenaDecorKind::Stand,
            {0.28F, 0.22F, 0.16F, 1.0F}, 0.0F,
        });

        auto banner_transform = glm::translate(
            glm::mat4 {1.0F},
            stand_position + glm::vec3 {0.0F, 3.1F, 0.0F});
        banner_transform =
            glm::scale(banner_transform, {0.86F, 2.2F, 0.08F});
        decor.push_back({
            banner_transform, IssouArenaDecorKind::Banner,
            side % 2U == 0U
                ? glm::vec4 {0.46F, 0.08F, 0.10F, 1.0F}
                : glm::vec4 {0.14F, 0.20F, 0.36F, 1.0F},
            0.0F,
        });
    }

    for (const auto& brazier : state.layout.braziers) {
        auto transform = glm::translate(
            glm::mat4 {1.0F},
            glm::vec3 {
                static_cast<float>(brazier.x) + 0.5F,
                static_cast<float>(brazier.y) + 0.72F,
                static_cast<float>(brazier.z) + 0.5F,
            });
        transform = glm::scale(transform, {0.72F, 1.44F, 0.72F});
        decor.push_back({
            transform, IssouArenaDecorKind::Brazier,
            {0.96F, 0.32F, 0.06F, 1.0F}, 1.0F,
        });
    }
    for (const auto& gate : state.layout.gate_cells) {
        auto transform = glm::translate(
            glm::mat4 {1.0F},
            glm::vec3 {
                static_cast<float>(gate.x) + 0.5F,
                static_cast<float>(gate.y) + 0.5F,
                static_cast<float>(gate.z) + 0.5F,
            });
        transform = glm::scale(transform, {1.0F, 1.0F, 1.0F});
        decor.push_back({
            transform, IssouArenaDecorKind::Gate,
            {0.22F, 0.18F, 0.14F, 1.0F}, 0.0F,
        });
    }

    if (state.chains_visible) {
        for (const auto& anchor : state.layout.chain_anchors) {
            const auto target =
                state.layout.colossus_spawn +
                glm::vec3 {0.0F, 2.9F, 0.0F};
            for (std::size_t link = 0U; link < 12U; ++link) {
                const auto t =
                    (static_cast<float>(link) + 0.5F) / 12.0F;
                const auto sag = std::sin(t * kPi) * 0.72F;
                auto transform = glm::translate(
                    glm::mat4 {1.0F},
                    glm::mix(anchor, target, t) -
                        glm::vec3 {0.0F, sag, 0.0F});
                transform = glm::scale(
                    transform, {0.14F, 0.30F, 0.14F});
                decor.push_back({
                    transform, IssouArenaDecorKind::ChainLink,
                    {0.20F, 0.22F, 0.24F, 1.0F}, 0.0F,
                });
            }
        }
    }
    return decor;
}

auto build_issou_arena_hud(
    const IssouArenaHudInput& raw_input)
    -> std::vector<IssouHudElement> {
    const auto width =
        safe_viewport(raw_input.viewport_width, 1920.0F);
    const auto height =
        safe_viewport(raw_input.viewport_height, 1080.0F);
    const auto requested_scale = finite_clamped(
        raw_input.accessibility.interface_scale,
        0.75F, 1.60F, 1.0F);
    const auto scale = std::clamp(
        std::min({
            requested_scale,
            width / 640.0F,
            height / 360.0F,
        }),
        0.50F, requested_scale);
    const auto opacity = finite_clamped(
        raw_input.accessibility.opacity, 0.45F, 1.0F, 0.88F);
    const auto background = raw_input.accessibility.high_contrast
        ? glm::vec4 {0.0F, 0.0F, 0.0F, opacity}
        : glm::vec4 {0.035F, 0.025F, 0.020F, opacity * 0.82F};
    const auto bar_width = std::min(width * 0.42F, 620.0F * scale);
    const auto bar_height = 14.0F * scale;
    const auto center_x = width * 0.5F - bar_width * 0.5F;

    std::vector<IssouHudElement> hud {};
    hud.reserve(6U);
    const auto combat_visible =
        raw_input.phase == IssouArenaPhase::Combat;
    hud.push_back({
        IssouHudElementKind::BossHealth,
        {center_x, 30.0F * scale, bar_width, bar_height},
        {0.68F, 0.10F, 0.075F, opacity}, background,
        finite_clamped(raw_input.boss_health_ratio, 0.0F, 1.0F),
        0.0F, combat_visible,
    });
    hud.push_back({
        IssouHudElementKind::BossStagger,
        {center_x, 49.0F * scale, bar_width, bar_height * 0.58F},
        {0.82F, 0.64F, 0.18F, opacity}, background,
        finite_clamped(raw_input.boss_stagger_ratio, 0.0F, 1.0F),
        0.0F, combat_visible,
    });
    hud.push_back({
        IssouHudElementKind::WeaponStability,
        {32.0F * scale, height - 80.0F * scale,
         220.0F * scale, bar_height * 0.72F},
        {0.32F, 0.62F, 0.80F, opacity}, background,
        finite_clamped(
            raw_input.weapon_stability_ratio, 0.0F, 1.0F),
        0.0F, combat_visible,
    });
    hud.push_back({
        IssouHudElementKind::Momentum,
        {32.0F * scale, height - 58.0F * scale,
         118.0F * scale, bar_height * 0.72F},
        {0.92F, 0.48F, 0.12F, opacity}, background,
        static_cast<float>(std::min<std::uint8_t>(
            raw_input.momentum, 3U)) / 3.0F,
        static_cast<float>(std::min<std::uint8_t>(
            raw_input.momentum, 3U)),
        combat_visible,
    });
    hud.push_back({
        IssouHudElementKind::Charge,
        {width - 252.0F * scale, height - 80.0F * scale,
         220.0F * scale, bar_height * 0.72F},
        {0.50F, 0.34F, 0.92F, opacity}, background,
        finite_clamped(raw_input.charge_ratio, 0.0F, 1.0F),
        0.0F, combat_visible,
    });
    hud.push_back({
        IssouHudElementKind::Countdown,
        {width * 0.5F - 48.0F * scale,
         height * 0.24F, 96.0F * scale, 42.0F * scale},
        {1.0F, 0.92F, 0.72F, opacity}, background,
        finite_clamped(
            raw_input.countdown_seconds /
                kIssouArenaCountdownSeconds,
            0.0F, 1.0F),
        std::max(0.0F, raw_input.countdown_seconds),
        raw_input.phase == IssouArenaPhase::Countdown,
    });
    return hud;
}

auto build_issou_results(
    const IssouArenaCombatStatistics& statistics,
    bool victory) -> IssouResultsPresentation {
    auto result = IssouResultsPresentation {};
    result.victory = victory;
    result.executed = statistics.executed;
    result.lines = {
        {IssouResultMetric::CombatSeconds,
         std::max(0.0F, statistics.combat_seconds), false},
        {IssouResultMetric::DamageDealt,
         std::max(0.0F, statistics.damage_dealt), victory},
        {IssouResultMetric::LimbsSevered,
         static_cast<float>(statistics.limbs_severed),
         statistics.limbs_severed > 0U},
        {IssouResultMetric::PerfectGuards,
         static_cast<float>(statistics.perfect_guards),
         statistics.perfect_guards > 0U},
        {IssouResultMetric::MissedAttacks,
         static_cast<float>(statistics.missed_attacks), false},
        {IssouResultMetric::MaximumMomentum,
         static_cast<float>(statistics.maximum_momentum),
         statistics.maximum_momentum >= 3U},
        {IssouResultMetric::MaximumTargetsHit,
         static_cast<float>(statistics.maximum_targets_hit),
         statistics.maximum_targets_hit >= 3U},
    };
    return result;
}

auto issou_crowd_variant_count(
    std::span<const IssouCrowdInstance> crowd) noexcept
    -> std::size_t {
    auto seen = std::array<bool, kIssouMaximumCrowdVariants> {};
    for (const auto& instance : crowd) {
        if (instance.variant < seen.size()) {
            seen[instance.variant] = true;
        }
    }
    return static_cast<std::size_t>(
        std::count(seen.begin(), seen.end(), true));
}

} // namespace valcraft
