#include "render/weapons/LeviathanWeaponPresentation.h"

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

[[nodiscard]] auto finite_vec3(
    const glm::vec3& value,
    const glm::vec3& fallback = glm::vec3 {0.0F}) noexcept
    -> glm::vec3 {
    return std::isfinite(value.x) &&
                   std::isfinite(value.y) &&
                   std::isfinite(value.z)
        ? value
        : fallback;
}

[[nodiscard]] auto finite_transform(
    const glm::mat4& value) noexcept -> glm::mat4 {
    for (int column = 0; column < 4; ++column) {
        for (int row = 0; row < 4; ++row) {
            if (!std::isfinite(value[column][row])) {
                return glm::mat4 {1.0F};
            }
        }
    }
    return value;
}

[[nodiscard]] auto make_part(const glm::vec3& center,
                             const glm::vec3& dimensions,
                             LeviathanVisualMaterial material,
                             LeviathanWeaponPartKind kind,
                             const glm::vec4& tint,
                             float roughness,
                             float emissive) -> LeviathanWeaponPartInstance {
    auto transform = glm::translate(glm::mat4 {1.0F}, center);
    transform = glm::scale(transform, dimensions);
    return {
        transform,
        material,
        kind,
        tint,
        roughness,
        emissive,
    };
}

[[nodiscard]] auto safe_awakening(
    LegendaryWeaponAwakening awakening) noexcept
    -> LegendaryWeaponAwakening {
    switch (awakening) {
    case LegendaryWeaponAwakening::Dormant:
    case LegendaryWeaponAwakening::Corrupted:
    case LegendaryWeaponAwakening::Astral:
    case LegendaryWeaponAwakening::Awakened:
        return awakening;
    }
    return LegendaryWeaponAwakening::Dormant;
}

[[nodiscard]] auto awakening_material(
    LegendaryWeaponAwakening awakening) noexcept
    -> LeviathanVisualMaterial {
    switch (safe_awakening(awakening)) {
    case LegendaryWeaponAwakening::Dormant:
        return LeviathanVisualMaterial::AncientBone;
    case LegendaryWeaponAwakening::Corrupted:
        return LeviathanVisualMaterial::CorruptedVein;
    case LegendaryWeaponAwakening::Astral:
        return LeviathanVisualMaterial::AstralRune;
    case LegendaryWeaponAwakening::Awakened:
        return LeviathanVisualMaterial::SovereignCore;
    }
    return LeviathanVisualMaterial::AncientBone;
}

[[nodiscard]] auto awakening_color(
    LegendaryWeaponAwakening awakening) noexcept -> glm::vec4 {
    switch (safe_awakening(awakening)) {
    case LegendaryWeaponAwakening::Dormant:
        return {0.54F, 0.42F, 0.29F, 1.0F};
    case LegendaryWeaponAwakening::Corrupted:
        return {0.64F, 0.06F, 0.10F, 1.0F};
    case LegendaryWeaponAwakening::Astral:
        return {0.22F, 0.58F, 0.96F, 1.0F};
    case LegendaryWeaponAwakening::Awakened:
        return {0.82F, 0.50F, 0.13F, 1.0F};
    }
    return {0.54F, 0.42F, 0.29F, 1.0F};
}

[[nodiscard]] auto pose_root(const LeviathanWeaponVisualInput& input)
    -> glm::mat4 {
    const auto progress =
        finite_clamped(input.state_progress, 0.0F, 1.0F);
    const auto charge =
        finite_clamped(input.charge_progress, 0.0F, 1.0F);
    const auto animation_time =
        std::isfinite(input.animation_time_seconds)
            ? input.animation_time_seconds
            : 0.0F;
    const auto first_person =
        input.view_mode == LeviathanViewMode::FirstPerson;

    auto position = first_person
        ? glm::vec3 {0.58F, -0.56F, -0.88F}
        : glm::vec3 {0.42F, 1.02F, -0.08F};
    auto rotation =
        first_person
            ? glm::vec3 {-0.28F, 0.22F, -0.48F}
            : glm::vec3 {-0.10F, 0.08F, -0.36F};

    if (input.state == ColossalWeaponState::Holstered) {
        position = first_person
            ? glm::vec3 {-0.72F, -0.45F, -1.15F}
            : glm::vec3 {-0.28F, 1.06F, 0.30F};
        rotation = {-0.18F, 0.24F, 2.44F};
    } else if (input.state == ColossalWeaponState::Sheathing) {
        const auto drawn_position = first_person
            ? glm::vec3 {0.58F, -0.56F, -0.88F}
            : glm::vec3 {0.42F, 1.02F, -0.08F};
        const auto back_position = first_person
            ? glm::vec3 {-0.72F, -0.45F, -1.15F}
            : glm::vec3 {-0.28F, 1.06F, 0.30F};
        position = glm::mix(drawn_position, back_position, progress);
        rotation = glm::mix(
            first_person
                ? glm::vec3 {-0.28F, 0.22F, -0.48F}
                : glm::vec3 {-0.10F, 0.08F, -0.36F},
            glm::vec3 {-0.18F, 0.24F, 2.44F},
            progress);
    } else if (input.state == ColossalWeaponState::Drawing) {
        const auto back_position = first_person
            ? glm::vec3 {-0.72F, -0.45F, -1.15F}
            : glm::vec3 {-0.28F, 1.06F, 0.30F};
        const auto drawn_position = first_person
            ? glm::vec3 {0.58F, -0.56F, -0.88F}
            : glm::vec3 {0.42F, 1.02F, -0.08F};
        position = glm::mix(back_position, drawn_position, progress);
        rotation = glm::mix(
            glm::vec3 {-0.18F, 0.24F, 2.44F},
            first_person
                ? glm::vec3 {-0.28F, 0.22F, -0.48F}
                : glm::vec3 {-0.10F, 0.08F, -0.36F},
            progress);
    } else if (input.state == ColossalWeaponState::Guard ||
               input.state == ColossalWeaponState::GuardBroken) {
        position += first_person
            ? glm::vec3 {-0.16F, 0.15F, 0.10F}
            : glm::vec3 {-0.14F, 0.13F, -0.03F};
        rotation = {-0.72F, -0.10F, -0.88F};
        if (input.state == ColossalWeaponState::GuardBroken) {
            rotation.z += progress * 0.74F;
            position.y -= progress * 0.28F;
        }
    } else if (input.state == ColossalWeaponState::Charge) {
        position += glm::vec3 {-0.18F, 0.12F, 0.12F};
        rotation = {-1.08F, 0.02F, -0.18F};
        const auto tremor =
            std::sin(animation_time * 37.0F) * 0.012F * charge;
        position.x += tremor;
        rotation.y += tremor * 2.0F;
    } else if (input.state == ColossalWeaponState::Windup ||
               input.state == ColossalWeaponState::Active ||
               input.state == ColossalWeaponState::Recovery ||
               input.state == ColossalWeaponState::Impact) {
        const auto arc = std::sin(progress * kPi * 0.5F);
        switch (input.attack) {
        case ColossalAttackKind::FirstSweep:
            rotation.z = -1.18F + arc * 2.34F;
            rotation.y = 0.32F - arc * 0.58F;
            break;
        case ColossalAttackKind::SecondSweep:
            rotation.z = 1.12F - arc * 2.18F;
            rotation.y = -0.24F + arc * 0.52F;
            break;
        case ColossalAttackKind::Earthbreaker:
        case ColossalAttackKind::ChargedExecution:
            rotation.x = -1.32F + arc * 1.78F;
            rotation.z = input.contextual_vertical ? -0.06F : -0.30F;
            position.z -= arc * 0.18F;
            break;
        case ColossalAttackKind::RunningCleave:
            rotation.x = -0.72F + arc * 0.74F;
            rotation.z = -1.02F + arc * 1.86F;
            position.z -= arc * 0.22F;
            break;
        case ColossalAttackKind::None:
            break;
        }
    }

    auto root = glm::translate(
        finite_transform(input.actor_transform), position);
    root = glm::rotate(root, rotation.y, {0.0F, 1.0F, 0.0F});
    root = glm::rotate(root, rotation.z, {0.0F, 0.0F, 1.0F});
    root = glm::rotate(root, rotation.x, {1.0F, 0.0F, 0.0F});
    return root;
}

[[nodiscard]] auto surface_color(
    LeviathanImpactSurface surface) noexcept -> glm::vec4 {
    switch (surface) {
    case LeviathanImpactSurface::Flesh:
        return {0.58F, 0.04F, 0.035F, 1.0F};
    case LeviathanImpactSurface::Wood:
        return {0.44F, 0.25F, 0.09F, 1.0F};
    case LeviathanImpactSurface::Stone:
        return {0.50F, 0.48F, 0.43F, 1.0F};
    case LeviathanImpactSurface::Metal:
        return {0.92F, 0.72F, 0.27F, 1.0F};
    case LeviathanImpactSurface::Sand:
        return {0.78F, 0.66F, 0.40F, 1.0F};
    case LeviathanImpactSurface::Water:
        return {0.32F, 0.66F, 0.86F, 0.85F};
    }
    return glm::vec4 {1.0F};
}

} // namespace

auto build_leviathan_weapon_model(
    LegendaryWeaponAwakening awakening)
    -> std::vector<LeviathanWeaponPartInstance> {
    const auto bone = glm::vec4 {0.73F, 0.66F, 0.50F, 1.0F};
    const auto iron = glm::vec4 {0.16F, 0.18F, 0.20F, 1.0F};
    const auto leather = glm::vec4 {0.22F, 0.105F, 0.055F, 1.0F};
    const auto accent = awakening_color(awakening);
    const auto accent_material = awakening_material(awakening);
    const auto emissive =
        safe_awakening(awakening) ==
                LegendaryWeaponAwakening::Dormant
            ? 0.0F
            : 0.48F +
                  static_cast<float>(
                      static_cast<std::uint8_t>(
                          safe_awakening(awakening))) *
                      0.14F;

    std::vector<LeviathanWeaponPartInstance> parts {};
    parts.reserve(18U);
    parts.push_back(make_part(
        {0.0F, 0.08F, 0.0F}, {0.24F, 0.16F, 0.18F},
        LeviathanVisualMaterial::DarkIron,
        LeviathanWeaponPartKind::Pommel, iron, 0.48F, 0.0F));
    parts.push_back(make_part(
        {0.0F, 0.42F, 0.0F}, {0.15F, 0.52F, 0.13F},
        LeviathanVisualMaterial::LeatherGrip,
        LeviathanWeaponPartKind::Grip, leather, 0.92F, 0.0F));
    parts.push_back(make_part(
        {0.0F, 1.05F, 0.0F}, {0.30F, 0.74F, 0.20F},
        LeviathanVisualMaterial::AncientBone,
        LeviathanWeaponPartKind::LowerSpine, bone, 0.76F, 0.0F));
    parts.push_back(make_part(
        {0.025F, 1.75F, 0.0F}, {0.46F, 0.66F, 0.23F},
        LeviathanVisualMaterial::AncientBone,
        LeviathanWeaponPartKind::UpperSpine, bone, 0.72F, 0.0F));
    parts.push_back(make_part(
        {0.06F, 2.14F, 0.0F}, {0.64F, 0.12F, 0.28F},
        LeviathanVisualMaterial::AncientBone,
        LeviathanWeaponPartKind::Crown, bone, 0.68F, 0.0F));

    for (const auto y : std::array<float, 4> {
             0.18F, 0.66F, 1.40F, 2.02F}) {
        parts.push_back(make_part(
            {0.0F, y, 0.0F}, {0.25F + y * 0.10F, 0.07F, 0.25F},
            LeviathanVisualMaterial::DarkIron,
            LeviathanWeaponPartKind::IronBand, iron, 0.40F, 0.0F));
    }

    if (safe_awakening(awakening) !=
        LegendaryWeaponAwakening::Dormant) {
        for (const auto y : std::array<float, 5> {
                 0.78F, 1.08F, 1.38F, 1.68F, 1.94F}) {
            parts.push_back(make_part(
                {0.0F, y, -0.112F},
                {0.055F, 0.16F, 0.018F},
                accent_material,
                LeviathanWeaponPartKind::AwakeningInlay,
                accent, 0.28F, emissive));
        }
    }
    return parts;
}

auto leviathan_weapon_local_bounds(
    std::span<const LeviathanWeaponPartInstance> parts) noexcept
    -> LeviathanWeaponLocalBounds {
    if (parts.empty()) {
        return {};
    }

    auto minimum =
        glm::vec3 {std::numeric_limits<float>::infinity()};
    auto maximum =
        glm::vec3 {-std::numeric_limits<float>::infinity()};
    for (const auto& part : parts) {
        const auto center = glm::vec3 {part.transform[3]};
        const auto half_extent = glm::vec3 {
            glm::length(glm::vec3 {part.transform[0]}),
            glm::length(glm::vec3 {part.transform[1]}),
            glm::length(glm::vec3 {part.transform[2]}),
        } * 0.5F;
        minimum = glm::min(minimum, center - half_extent);
        maximum = glm::max(maximum, center + half_extent);
    }
    return {minimum, maximum};
}

auto solve_leviathan_weapon_pose(
    const LeviathanWeaponVisualInput& input) -> LeviathanWeaponPose {
    auto pose = LeviathanWeaponPose {};
    pose.root_transform = pose_root(input);
    pose.carried_on_back =
        input.state == ColossalWeaponState::Holstered ||
        input.state == ColossalWeaponState::Sheathing;
    pose.visible =
        input.state != ColossalWeaponState::Holstered ||
        input.view_mode == LeviathanViewMode::ThirdPerson;

    pose.parts = build_leviathan_weapon_model(input.awakening);
    for (auto& part : pose.parts) {
        part.transform = pose.root_transform * part.transform;
    }
    pose.primary_hand_anchor = glm::vec3 {
        pose.root_transform * glm::vec4 {0.0F, 0.31F, 0.0F, 1.0F}};
    pose.secondary_hand_anchor = glm::vec3 {
        pose.root_transform * glm::vec4 {0.0F, 0.57F, 0.0F, 1.0F}};
    return pose;
}

auto leviathan_visual_hit_stop_seconds(
    LeviathanImpactWeight weight) noexcept -> float {
    switch (weight) {
    case LeviathanImpactWeight::Light:
        return 0.038F;
    case LeviathanImpactWeight::Heavy:
        return 0.060F;
    case LeviathanImpactWeight::BossOrSection:
        return 0.082F;
    }
    return 0.038F;
}

auto build_leviathan_visual_events(
    const LeviathanCombatVisualRequest& request)
    -> std::vector<LeviathanVisualEvent> {
    const auto effect_scale = finite_clamped(
        request.accessibility.effect_intensity_scale,
        0.0F, 1.0F, 1.0F);
    const auto camera_scale = request.accessibility.reduced_motion
        ? 0.0F
        : finite_clamped(
              request.accessibility.camera_motion_scale,
              0.0F, 1.0F, 1.0F);
    const auto progress =
        finite_clamped(request.attack_progress, 0.0F, 1.0F);
    auto direction = request.direction;
    if (!std::isfinite(direction.x) ||
        !std::isfinite(direction.y) ||
        !std::isfinite(direction.z) ||
        glm::length(direction) <= 1.0e-5F) {
        direction = {0.0F, 0.0F, -1.0F};
    } else {
        direction = glm::normalize(direction);
    }
    const auto accent = awakening_color(request.awakening);

    const auto origin = finite_vec3(request.origin);
    std::vector<LeviathanVisualEvent> events {};
    events.reserve(5U);
    if (request.attack != ColossalAttackKind::None &&
        progress >= 0.18F) {
        events.push_back({
            LeviathanVisualEventKind::Trail,
            origin,
            direction,
            accent,
            1.20F + progress * 1.0F,
            request.accessibility.reduced_motion ? 0.045F : 0.11F,
            effect_scale * (0.35F + progress * 0.65F),
            0U,
            true,
        });
    }
    if (!request.landed) {
        return events;
    }

    const auto heavy =
        request.weight != LeviathanImpactWeight::Light;
    const auto reduced_flash =
        request.accessibility.reduced_flashes ? 0.48F : 1.0F;
    events.push_back({
        LeviathanVisualEventKind::ImpactBurst,
        origin,
        direction,
        surface_color(request.surface),
        heavy ? 1.15F : 0.68F,
        heavy ? 0.28F : 0.18F,
        effect_scale * reduced_flash,
        static_cast<std::uint16_t>(
            std::lround((heavy ? 22.0F : 12.0F) * effect_scale)),
        true,
    });

    if (request.attack == ColossalAttackKind::Earthbreaker ||
        request.attack == ColossalAttackKind::ChargedExecution) {
        const auto charged =
            request.attack == ColossalAttackKind::ChargedExecution;
        events.push_back({
            LeviathanVisualEventKind::Shockwave,
            origin,
            {0.0F, 1.0F, 0.0F},
            accent,
            charged ? 4.0F : 2.5F,
            charged ? 0.52F : 0.36F,
            effect_scale * reduced_flash,
            static_cast<std::uint16_t>(
                std::lround((charged ? 28.0F : 16.0F) *
                            effect_scale)),
            true,
        });
    }

    if (camera_scale > 0.0F) {
        events.push_back({
            LeviathanVisualEventKind::CameraImpulse,
            origin,
            -direction,
            glm::vec4 {1.0F},
            0.0F,
            heavy ? 0.14F : 0.08F,
            camera_scale * (heavy ? 0.72F : 0.32F),
            0U,
            true,
        });
    }

    events.push_back({
        LeviathanVisualEventKind::VisualHitStop,
        origin,
        direction,
        glm::vec4 {1.0F},
        0.0F,
        leviathan_visual_hit_stop_seconds(
            request.sectioned
                ? LeviathanImpactWeight::BossOrSection
                : request.weight),
        1.0F,
        0U,
        true,
    });
    return events;
}

} // namespace valcraft
