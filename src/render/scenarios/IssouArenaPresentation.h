#pragma once

#include "creatures/CreatureGeometry.h"
#include "gameplay/scenarios/IssouArenaScenario.h"

#include <glm/mat4x4.hpp>
#include <glm/vec3.hpp>
#include <glm/vec4.hpp>

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace valcraft {

inline constexpr std::size_t kIssouMinimumCrowdSize = 100U;
inline constexpr std::size_t kIssouMaximumCrowdSize = 180U;
inline constexpr std::uint8_t kIssouMinimumCrowdVariants = 8U;
inline constexpr std::uint8_t kIssouMaximumCrowdVariants = 16U;

enum class IssouCrowdReaction : std::uint8_t {
    Idle = 0,
    Murmur,
    Cheer,
    Roar,
    Flinch,
    Boo,
    Silence,
};

enum class IssouCrowdLod : std::uint8_t {
    Full = 0,
    Simplified,
    Impostor,
    Culled,
};

struct IssouCrowdRequest {
    glm::vec3 camera_position {0.0F};
    std::size_t desired_count = 140U;
    std::uint8_t variant_count = 12U;
    std::uint32_t seed = 1U;
    float animation_seconds = 0.0F;
    float excitement = 0.0F;
    IssouArenaEventKind latest_event =
        IssouArenaEventKind::CrowdMurmur;
    bool reduced_motion = false;
};

struct IssouCrowdInstance {
    glm::vec3 position {0.0F};
    float yaw_radians = 0.0F;
    float animation_phase = 0.0F;
    float reaction_amount = 0.0F;
    glm::vec4 cloth_color {1.0F};
    IssouCrowdReaction reaction =
        IssouCrowdReaction::Idle;
    IssouCrowdLod lod = IssouCrowdLod::Full;
    std::uint16_t instance_id = 0U;
    std::uint8_t variant = 0U;
};

enum class IssouArenaDecorKind : std::uint8_t {
    Stand = 0,
    ChainLink,
    Brazier,
    Banner,
    Gate,
};

struct IssouArenaDecorInstance {
    glm::mat4 transform {1.0F};
    IssouArenaDecorKind kind = IssouArenaDecorKind::Stand;
    glm::vec4 color {1.0F};
    float emissive_strength = 0.0F;
};

struct IssouScreenRect {
    float x = 0.0F;
    float y = 0.0F;
    float width = 0.0F;
    float height = 0.0F;
};

enum class IssouHudElementKind : std::uint8_t {
    BossHealth = 0,
    BossStagger,
    WeaponStability,
    Momentum,
    Charge,
    Countdown,
};

struct IssouHudElement {
    IssouHudElementKind kind =
        IssouHudElementKind::BossHealth;
    IssouScreenRect rect {};
    glm::vec4 foreground {1.0F};
    glm::vec4 background {0.0F, 0.0F, 0.0F, 0.72F};
    float value = 0.0F;
    float secondary_value = 0.0F;
    bool visible = false;
};

struct IssouHudAccessibility {
    float interface_scale = 1.0F;
    float opacity = 0.88F;
    bool high_contrast = false;
    bool reduced_motion = false;
};

struct IssouArenaHudInput {
    IssouArenaPhase phase = IssouArenaPhase::Inactive;
    float viewport_width = 1920.0F;
    float viewport_height = 1080.0F;
    float boss_health_ratio = 1.0F;
    float boss_stagger_ratio = 0.0F;
    float weapon_stability_ratio = 1.0F;
    float charge_ratio = 0.0F;
    float countdown_seconds = 0.0F;
    std::uint8_t momentum = 0U;
    IssouHudAccessibility accessibility {};
};

enum class IssouResultMetric : std::uint8_t {
    CombatSeconds = 0,
    DamageDealt,
    LimbsSevered,
    PerfectGuards,
    MissedAttacks,
    MaximumMomentum,
    MaximumTargetsHit,
};

struct IssouResultLine {
    IssouResultMetric metric =
        IssouResultMetric::CombatSeconds;
    float value = 0.0F;
    bool highlight = false;
};

struct IssouResultsPresentation {
    bool victory = false;
    bool executed = false;
    std::vector<IssouResultLine> lines {};
};

[[nodiscard]] auto build_issou_crowd(
    const IssouArenaLayout& layout,
    const IssouCrowdRequest& request)
    -> std::vector<IssouCrowdInstance>;
[[nodiscard]] auto build_issou_crowd_member_parts(
    const IssouCrowdInstance& instance)
    -> std::vector<CreaturePartInstance>;
[[nodiscard]] auto build_issou_arena_decor(
    const IssouArenaState& state)
    -> std::vector<IssouArenaDecorInstance>;
[[nodiscard]] auto build_issou_arena_hud(
    const IssouArenaHudInput& input)
    -> std::vector<IssouHudElement>;
[[nodiscard]] auto build_issou_results(
    const IssouArenaCombatStatistics& statistics,
    bool victory) -> IssouResultsPresentation;
[[nodiscard]] auto issou_crowd_variant_count(
    std::span<const IssouCrowdInstance> crowd) noexcept
    -> std::size_t;

} // namespace valcraft
