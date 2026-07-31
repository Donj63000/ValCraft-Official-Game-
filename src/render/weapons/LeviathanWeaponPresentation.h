#pragma once

#include "gameplay/weapons/ColossalWeaponState.h"
#include "gameplay/weapons/LegendaryWeaponProgression.h"

#include <glm/mat4x4.hpp>
#include <glm/vec3.hpp>
#include <glm/vec4.hpp>

#include <cstdint>
#include <span>
#include <vector>

namespace valcraft {

inline constexpr float kLeviathanWeaponVisualLengthBlocks = 2.20F;

enum class LeviathanViewMode : std::uint8_t {
    FirstPerson = 0,
    ThirdPerson,
};

enum class LeviathanVisualMaterial : std::uint8_t {
    AncientBone = 0,
    DarkIron,
    LeatherGrip,
    CorruptedVein,
    AstralRune,
    SovereignCore,
};

enum class LeviathanWeaponPartKind : std::uint8_t {
    Pommel = 0,
    Grip,
    LowerSpine,
    UpperSpine,
    Crown,
    IronBand,
    AwakeningInlay,
};

struct LeviathanWeaponPartInstance {
    glm::mat4 transform {1.0F};
    LeviathanVisualMaterial material =
        LeviathanVisualMaterial::AncientBone;
    LeviathanWeaponPartKind kind =
        LeviathanWeaponPartKind::LowerSpine;
    glm::vec4 color_tint {1.0F};
    float roughness = 0.75F;
    float emissive_strength = 0.0F;
};

struct LeviathanWeaponVisualInput {
    glm::mat4 actor_transform {1.0F};
    LeviathanViewMode view_mode = LeviathanViewMode::FirstPerson;
    ColossalWeaponState state = ColossalWeaponState::Holstered;
    ColossalAttackKind attack = ColossalAttackKind::None;
    LegendaryWeaponAwakening awakening =
        LegendaryWeaponAwakening::Dormant;
    float state_progress = 0.0F;
    float charge_progress = 0.0F;
    float animation_time_seconds = 0.0F;
    bool contextual_vertical = false;
};

struct LeviathanWeaponPose {
    glm::mat4 root_transform {1.0F};
    glm::vec3 primary_hand_anchor {0.0F};
    glm::vec3 secondary_hand_anchor {0.0F};
    std::vector<LeviathanWeaponPartInstance> parts {};
    bool visible = true;
    bool carried_on_back = false;
};

struct LeviathanWeaponLocalBounds {
    glm::vec3 minimum {0.0F};
    glm::vec3 maximum {0.0F};
};

enum class LeviathanImpactSurface : std::uint8_t {
    Flesh = 0,
    Wood,
    Stone,
    Metal,
    Sand,
    Water,
};

enum class LeviathanImpactWeight : std::uint8_t {
    Light = 0,
    Heavy,
    BossOrSection,
};

enum class LeviathanVisualEventKind : std::uint8_t {
    Trail = 0,
    ImpactBurst,
    Shockwave,
    CameraImpulse,
    VisualHitStop,
};

struct LeviathanPresentationAccessibility {
    float camera_motion_scale = 1.0F;
    float effect_intensity_scale = 1.0F;
    bool reduced_motion = false;
    bool reduced_flashes = false;
};

struct LeviathanCombatVisualRequest {
    glm::vec3 origin {0.0F};
    glm::vec3 direction {0.0F, 0.0F, -1.0F};
    ColossalAttackKind attack = ColossalAttackKind::None;
    LeviathanImpactSurface surface =
        LeviathanImpactSurface::Flesh;
    LeviathanImpactWeight weight =
        LeviathanImpactWeight::Light;
    LegendaryWeaponAwakening awakening =
        LegendaryWeaponAwakening::Dormant;
    LeviathanPresentationAccessibility accessibility {};
    float attack_progress = 0.0F;
    bool landed = false;
    bool sectioned = false;
};

struct LeviathanVisualEvent {
    LeviathanVisualEventKind kind =
        LeviathanVisualEventKind::Trail;
    glm::vec3 position {0.0F};
    glm::vec3 direction {0.0F, 1.0F, 0.0F};
    glm::vec4 color {1.0F};
    float radius = 0.0F;
    float duration_seconds = 0.0F;
    float intensity = 0.0F;
    std::uint16_t particle_count = 0U;
    // Je rends explicite que ce gel appartient uniquement a la presentation.
    bool visual_only = true;
};

[[nodiscard]] auto build_leviathan_weapon_model(
    LegendaryWeaponAwakening awakening)
    -> std::vector<LeviathanWeaponPartInstance>;
[[nodiscard]] auto leviathan_weapon_local_bounds(
    std::span<const LeviathanWeaponPartInstance> parts) noexcept
    -> LeviathanWeaponLocalBounds;
[[nodiscard]] auto solve_leviathan_weapon_pose(
    const LeviathanWeaponVisualInput& input) -> LeviathanWeaponPose;
[[nodiscard]] auto build_leviathan_visual_events(
    const LeviathanCombatVisualRequest& request)
    -> std::vector<LeviathanVisualEvent>;
[[nodiscard]] auto leviathan_visual_hit_stop_seconds(
    LeviathanImpactWeight weight) noexcept -> float;

} // namespace valcraft
