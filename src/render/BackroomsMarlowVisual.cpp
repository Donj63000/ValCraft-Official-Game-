#include "render/BackroomsMarlowVisual.h"

#include <glm/common.hpp>
#include <glm/geometric.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/vec4.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>

namespace valcraft {

namespace {

constexpr float kPi = 3.14159265358979323846F;
constexpr float kTwoPi = 2.0F * kPi;
constexpr float kMinimumPartExtent = 1.0e-4F;

constexpr float kMaterialWetSkin = 0.44F;
constexpr float kMaterialWetRot = 0.56F;
constexpr float kMaterialSoakedFabric = 0.24F;
constexpr float kMaterialMilkyEye = 0.88F;
constexpr float kMaterialRubber = 0.78F;

struct MarlowBuildContext {
    float tension = 0.90F;
    float sky_light = 0.0F;
    float block_light = 0.0F;
};

[[nodiscard]] auto finite_or(float value, float fallback) noexcept -> float {
    return std::isfinite(value) ? value : fallback;
}

[[nodiscard]] auto finite_position(const glm::vec3& value) noexcept
    -> glm::vec3 {
    return {
        finite_or(value.x, 0.0F),
        finite_or(value.y, 0.0F),
        finite_or(value.z, 0.0F),
    };
}

[[nodiscard]] auto saturate(float value) noexcept -> float {
    return glm::clamp(finite_or(value, 0.0F), 0.0F, 1.0F);
}

[[nodiscard]] auto signed_unit(float value) noexcept -> float {
    return glm::clamp(finite_or(value, 0.0F), -1.0F, 1.0F);
}

[[nodiscard]] auto smoothstep01(float value) noexcept -> float {
    const auto clamped = saturate(value);
    return clamped * clamped * (3.0F - 2.0F * clamped);
}

[[nodiscard]] auto periodic_angle(float value) noexcept -> float {
    if (!std::isfinite(value)) {
        return 0.0F;
    }
    return static_cast<float>(
        std::remainder(
            static_cast<double>(value),
            static_cast<double>(kTwoPi)));
}

[[nodiscard]] auto wave(
    float animation_time,
    float frequency,
    float phase = 0.0F) noexcept -> float {
    if (!std::isfinite(animation_time)) {
        return std::sin(phase);
    }
    const auto angle = std::remainder(
        static_cast<double>(animation_time) *
                static_cast<double>(frequency) +
            static_cast<double>(phase),
        static_cast<double>(kTwoPi));
    return static_cast<float>(std::sin(angle));
}

[[nodiscard]] auto uniform_uvs(CreatureAtlasTile tile) noexcept
    -> std::array<BoxUvRect, 6> {
    constexpr auto kStep = 1.0F / kCreatureAtlasTilesPerAxis;
    const auto index = static_cast<int>(tile);
    const auto tile_x = index % static_cast<int>(kCreatureAtlasTilesPerAxis);
    const auto tile_y = index / static_cast<int>(kCreatureAtlasTilesPerAxis);
    const auto rectangle = BoxUvRect {
        static_cast<float>(tile_x) * kStep,
        static_cast<float>(tile_y) * kStep,
        static_cast<float>(tile_x + 1) * kStep,
        static_cast<float>(tile_y + 1) * kStep,
    };
    std::array<BoxUvRect, 6> result {};
    result.fill(rectangle);
    return result;
}

[[nodiscard]] auto make_box_transform(
    const glm::mat4& root,
    const glm::vec3& center,
    const glm::vec3& half_extent,
    const glm::vec3& rotation_radians) noexcept -> glm::mat4 {
    auto transform = glm::translate(root, center);
    transform = glm::rotate(
        transform,
        rotation_radians.y,
        glm::vec3 {0.0F, 1.0F, 0.0F});
    transform = glm::rotate(
        transform,
        rotation_radians.z,
        glm::vec3 {0.0F, 0.0F, 1.0F});
    transform = glm::rotate(
        transform,
        rotation_radians.x,
        glm::vec3 {1.0F, 0.0F, 0.0F});
    return glm::scale(transform, half_extent * 2.0F);
}

void append_part(
    std::vector<CreaturePartInstance>& parts,
    std::size_t budget,
    const glm::mat4& transform,
    CreatureAtlasTile tile,
    float material_class,
    float cavity_mask,
    float local_light_boost,
    const MarlowBuildContext& context) {
    if (parts.size() >= budget) {
        return;
    }

    CreaturePartInstance part {};
    part.transform = transform;
    part.face_uvs = uniform_uvs(tile);
    part.nightmare_factor = 0.92F;
    part.tension = context.tension;
    part.material_class = glm::clamp(material_class, 0.0F, 1.0F);
    part.cavity_mask = glm::clamp(cavity_mask, 0.0F, 1.0F);
    // Je n'utilise pas le canal emissif actuel, teinte en rouge pour Jack.
    // Les yeux de Marlow restent blancs grace a un tres leger apport local.
    part.emissive_strength = 0.0F;
    part.sky_light = context.sky_light;
    part.block_light = glm::clamp(
        context.block_light + std::max(local_light_boost, 0.0F),
        0.0F,
        1.0F);
    // Je marque toutes les surfaces comme mouillables. Dans les Poolrooms,
    // cela reste sans effet meteorologique mais preserve le fallback commun.
    part.precipitation_exposure = 1.0F;
    parts.push_back(part);
}

void append_box(
    std::vector<CreaturePartInstance>& parts,
    std::size_t budget,
    const glm::mat4& root,
    const glm::vec3& center,
    const glm::vec3& half_extent,
    const glm::vec3& rotation_radians,
    CreatureAtlasTile tile,
    float material_class,
    float cavity_mask,
    float local_light_boost,
    const MarlowBuildContext& context) {
    if (half_extent.x <= kMinimumPartExtent ||
        half_extent.y <= kMinimumPartExtent ||
        half_extent.z <= kMinimumPartExtent) {
        return;
    }
    append_part(
        parts,
        budget,
        make_box_transform(root, center, half_extent, rotation_radians),
        tile,
        material_class,
        cavity_mask,
        local_light_boost,
        context);
}

void append_segment(
    std::vector<CreaturePartInstance>& parts,
    std::size_t budget,
    const glm::mat4& root,
    const glm::vec3& start,
    const glm::vec3& end,
    float half_width,
    float half_depth,
    float overlap,
    CreatureAtlasTile tile,
    float material_class,
    float cavity_mask,
    float local_light_boost,
    const MarlowBuildContext& context) {
    const auto axis = end - start;
    const auto length = glm::length(axis);
    if (!std::isfinite(length) || length <= kMinimumPartExtent ||
        half_width <= kMinimumPartExtent ||
        half_depth <= kMinimumPartExtent) {
        return;
    }

    const auto direction = axis / length;
    const auto reference =
        std::abs(direction.z) < 0.88F
            ? glm::vec3 {0.0F, 0.0F, 1.0F}
            : glm::vec3 {1.0F, 0.0F, 0.0F};
    const auto local_x = glm::normalize(glm::cross(reference, direction));
    const auto local_z = glm::normalize(glm::cross(direction, local_x));

    auto local_transform = glm::mat4 {1.0F};
    local_transform[0] = glm::vec4 {local_x * (half_width * 2.0F), 0.0F};
    local_transform[1] = glm::vec4 {
        direction * (length + std::max(overlap, 0.0F) * 2.0F),
        0.0F,
    };
    local_transform[2] = glm::vec4 {local_z * (half_depth * 2.0F), 0.0F};
    local_transform[3] = glm::vec4 {(start + end) * 0.5F, 1.0F};
    append_part(
        parts,
        budget,
        root * local_transform,
        tile,
        material_class,
        cavity_mask,
        local_light_boost,
        context);
}

void append_legs(
    std::vector<CreaturePartInstance>& parts,
    const glm::mat4& root,
    float stride,
    float motion,
    float body_bob,
    const MarlowBuildContext& context) {
    for (const auto side : {-1.0F, 1.0F}) {
        const auto phase = side < 0.0F ? 1.0F : -1.0F;
        const auto step = stride * phase * motion;
        const glm::vec3 hip {
            0.0F,
            1.88F + body_bob,
            side * 0.235F,
        };
        const glm::vec3 knee {
            step * 0.075F,
            1.04F + std::max(step, 0.0F) * 0.035F,
            side * 0.245F,
        };
        const glm::vec3 ankle {
            step * 0.17F,
            0.24F + std::max(step, 0.0F) * 0.075F,
            side * 0.225F,
        };
        append_segment(
            parts,
            kBackroomsMarlowVisualPartBudget,
            root,
            hip,
            knee,
            0.145F,
            0.155F,
            0.035F,
            CreatureAtlasTile::MarlowUniform,
            kMaterialSoakedFabric,
            0.38F,
            0.0F,
            context);
        append_segment(
            parts,
            kBackroomsMarlowVisualPartBudget,
            root,
            knee,
            ankle,
            0.125F,
            0.135F,
            0.040F,
            CreatureAtlasTile::MarlowSkin,
            kMaterialWetSkin,
            0.48F,
            0.0F,
            context);
        append_box(
            parts,
            kBackroomsMarlowVisualPartBudget,
            root,
            knee,
            glm::vec3 {0.155F, 0.155F, 0.165F},
            glm::vec3 {0.0F},
            side < 0.0F
                ? CreatureAtlasTile::MarlowRot
                : CreatureAtlasTile::MarlowSkin,
            side < 0.0F ? kMaterialWetRot : kMaterialWetSkin,
            0.48F,
            0.0F,
            context);
        append_box(
            parts,
            kBackroomsMarlowVisualPartBudget,
            root,
            glm::vec3 {
                ankle.x + 0.145F,
                0.12F + std::max(step, 0.0F) * 0.075F,
                ankle.z,
            },
            glm::vec3 {0.255F, 0.12F, 0.17F},
            glm::vec3 {0.0F, 0.0F, -step * 0.06F},
            CreatureAtlasTile::MarlowRot,
            kMaterialWetRot,
            0.57F,
            0.0F,
            context);
    }
}

void append_torso(
    std::vector<CreaturePartInstance>& parts,
    const glm::mat4& upper_root,
    float breath,
    float body_sway,
    const MarlowBuildContext& context) {
    append_box(
        parts,
        kBackroomsMarlowVisualPartBudget,
        upper_root,
        glm::vec3 {0.0F, 1.84F, 0.0F},
        glm::vec3 {0.29F, 0.25F, 0.38F},
        glm::vec3 {0.0F, 0.0F, body_sway * 0.10F},
        CreatureAtlasTile::MarlowUniform,
        kMaterialSoakedFabric,
        0.45F,
        0.0F,
        context);
    append_box(
        parts,
        kBackroomsMarlowVisualPartBudget,
        upper_root,
        glm::vec3 {0.015F, 2.24F + breath, 0.0F},
        glm::vec3 {0.36F, 0.38F, 0.43F},
        glm::vec3 {0.0F, 0.0F, -0.035F + body_sway * 0.08F},
        CreatureAtlasTile::MarlowUniform,
        kMaterialSoakedFabric,
        0.42F,
        0.0F,
        context);
    append_box(
        parts,
        kBackroomsMarlowVisualPartBudget,
        upper_root,
        glm::vec3 {0.035F, 2.66F + breath, 0.0F},
        glm::vec3 {0.40F, 0.40F, 0.48F},
        glm::vec3 {0.0F, 0.0F, -0.055F + body_sway * 0.10F},
        CreatureAtlasTile::MarlowUniform,
        kMaterialSoakedFabric,
        0.47F,
        0.0F,
        context);

    // Je dechire le bas de l'uniforme en panneaux dissymetriques. Le rendu
    // moderne les classe comme panneaux/rubans, le fallback garde leurs boites.
    constexpr std::array<float, 4> kHemOffsets {{
        -0.32F,
        -0.105F,
        0.12F,
        0.34F,
    }};
    constexpr std::array<float, 4> kHemLengths {{
        0.20F,
        0.33F,
        0.27F,
        0.17F,
    }};
    for (std::size_t index = 0U; index < kHemOffsets.size(); ++index) {
        const auto centered =
            static_cast<float>(index) - 1.5F;
        append_box(
            parts,
            kBackroomsMarlowVisualPartBudget,
            upper_root,
            glm::vec3 {
                -0.015F + body_sway * (0.025F + 0.008F * centered),
                1.57F - kHemLengths[index] * 0.16F,
                kHemOffsets[index],
            },
            glm::vec3 {0.025F, kHemLengths[index], 0.105F},
            glm::vec3 {
                centered * 0.025F,
                centered * 0.020F,
                body_sway * 0.16F + centered * 0.035F,
            },
            CreatureAtlasTile::MarlowUniform,
            kMaterialSoakedFabric,
            0.51F,
            0.0F,
            context);
    }

    // Je fais remonter deux plaies huileuses a travers le tissu, sans symbole
    // lisible qui rendrait la silhouette moins universelle.
    append_box(
        parts,
        kBackroomsMarlowVisualPartBudget,
        upper_root,
        glm::vec3 {0.402F, 2.45F + breath, -0.20F},
        glm::vec3 {0.025F, 0.22F, 0.105F},
        glm::vec3 {0.05F, 0.10F, -0.18F},
        CreatureAtlasTile::MarlowRot,
        kMaterialWetRot,
        0.72F,
        0.0F,
        context);
    append_box(
        parts,
        kBackroomsMarlowVisualPartBudget,
        upper_root,
        glm::vec3 {0.425F, 2.70F + breath, 0.24F},
        glm::vec3 {0.022F, 0.15F, 0.09F},
        glm::vec3 {-0.08F, -0.06F, 0.16F},
        CreatureAtlasTile::MarlowRot,
        kMaterialWetRot,
        0.78F,
        0.0F,
        context);

    append_segment(
        parts,
        kBackroomsMarlowVisualPartBudget,
        upper_root,
        glm::vec3 {0.02F, 2.93F + breath, 0.0F},
        glm::vec3 {0.06F, 3.24F + breath, 0.0F},
        0.135F,
        0.145F,
        0.025F,
        CreatureAtlasTile::MarlowSkin,
        kMaterialWetSkin,
        0.61F,
        0.0F,
        context);
}

void append_head(
    std::vector<CreaturePartInstance>& parts,
    const glm::mat4& upper_root,
    float animation_time,
    float breath,
    float head_scan,
    float peek,
    float reach,
    float jumpscare,
    const MarlowBuildContext& context) {
    const glm::vec3 pivot {0.06F, 3.20F + breath, 0.0F};
    auto head_root = glm::translate(upper_root, pivot);
    head_root = glm::rotate(
        head_root,
        head_scan,
        glm::vec3 {0.0F, 1.0F, 0.0F});
    head_root = glm::rotate(
        head_root,
        wave(animation_time, 0.41F) * 0.032F +
            peek * 0.105F - jumpscare * 0.065F,
        glm::vec3 {0.0F, 0.0F, 1.0F});
    head_root = glm::translate(head_root, -pivot);
    head_root = glm::translate(
        head_root,
        glm::vec3 {
            reach * 0.10F + jumpscare * 0.34F,
            -jumpscare * 0.08F,
            peek * 0.055F,
        });

    append_box(
        parts,
        kBackroomsMarlowVisualPartBudget,
        head_root,
        glm::vec3 {0.075F, 3.48F + breath, 0.0F},
        glm::vec3 {0.29F, 0.34F, 0.31F},
        glm::vec3 {0.0F, 0.0F, -0.025F},
        CreatureAtlasTile::MarlowSkin,
        kMaterialWetSkin,
        0.65F,
        0.0F,
        context);

    // Je pose un bonnet unique au-dessus du front. Aucune piece ne relie le
    // bonnet aux yeux : il ne peut donc jamais etre lu comme un masque.
    append_box(
        parts,
        kBackroomsMarlowVisualPartBudget,
        head_root,
        glm::vec3 {0.045F, 3.735F + breath, 0.0F},
        glm::vec3 {0.295F, 0.115F, 0.325F},
        glm::vec3 {0.0F, 0.0F, -0.018F},
        CreatureAtlasTile::MarlowSwimCap,
        kMaterialRubber,
        0.25F,
        0.0F,
        context);

    for (const auto side : {-1.0F, 1.0F}) {
        // Une orbite pourrie plus large fait paraitre chaque globe encore plus
        // ouvert, sans dessiner ni paupiere ni pupille sur le blanc.
        append_box(
            parts,
            kBackroomsMarlowVisualPartBudget,
            head_root,
            glm::vec3 {0.326F, 3.535F + breath, side * 0.158F},
            glm::vec3 {0.052F, 0.205F, 0.167F},
            glm::vec3 {0.0F, 0.0F, side * 0.045F},
            CreatureAtlasTile::MarlowRot,
            kMaterialWetRot,
            0.92F,
            0.0F,
            context);
        append_box(
            parts,
            kBackroomsMarlowVisualPartBudget,
            head_root,
            glm::vec3 {0.372F, 3.545F + breath, side * 0.158F},
            glm::vec3 {0.077F, 0.172F, 0.139F},
            glm::vec3 {0.0F, 0.0F, side * 0.025F},
            CreatureAtlasTile::MarlowEyeWhite,
            kMaterialMilkyEye,
            0.04F,
            0.075F,
            context);
    }

    // Je creuse une bouche dechiree, longue et noire, sans machoire reguliere.
    append_box(
        parts,
        kBackroomsMarlowVisualPartBudget,
        head_root,
        glm::vec3 {0.366F, 3.245F + breath, 0.0F},
        glm::vec3 {0.060F, 0.180F, 0.152F},
        glm::vec3 {0.0F, 0.0F, 0.015F},
        CreatureAtlasTile::MarlowRot,
        kMaterialWetRot,
        0.98F,
        0.0F,
        context);
    constexpr std::array<float, 4> kTearSides {{
        -1.0F,
        -0.45F,
        0.48F,
        1.0F,
    }};
    for (std::size_t index = 0U; index < kTearSides.size(); ++index) {
        const auto side = kTearSides[index];
        const auto length = 0.11F + static_cast<float>(index % 2U) * 0.055F;
        append_segment(
            parts,
            kBackroomsMarlowVisualPartBudget,
            head_root,
            glm::vec3 {0.391F, 3.17F + breath, side * 0.13F},
            glm::vec3 {
                0.374F,
                3.17F - length + breath,
                side * (0.15F + 0.015F * length),
            },
            0.025F,
            0.022F,
            0.012F,
            CreatureAtlasTile::MarlowRot,
            kMaterialWetRot,
            0.90F,
            0.0F,
            context);
    }

    append_box(
        parts,
        kBackroomsMarlowVisualPartBudget,
        head_root,
        glm::vec3 {0.347F, 3.35F + breath, -0.275F},
        glm::vec3 {0.036F, 0.105F, 0.070F},
        glm::vec3 {0.0F, 0.0F, -0.25F},
        CreatureAtlasTile::MarlowRot,
        kMaterialWetRot,
        0.84F,
        0.0F,
        context);
    append_box(
        parts,
        kBackroomsMarlowVisualPartBudget,
        head_root,
        glm::vec3 {0.350F, 3.67F + breath, 0.245F},
        glm::vec3 {0.032F, 0.075F, 0.065F},
        glm::vec3 {0.0F, 0.0F, 0.18F},
        CreatureAtlasTile::MarlowRot,
        kMaterialWetRot,
        0.82F,
        0.0F,
        context);
}

void append_arm(
    std::vector<CreaturePartInstance>& parts,
    const glm::mat4& upper_root,
    float side,
    float stride,
    float animation_time,
    float motion,
    float reach,
    float jumpscare,
    const MarlowBuildContext& context) {
    const auto asymmetry = side < 0.0F ? -0.055F : 0.035F;
    const auto dead_sway =
        wave(animation_time, 0.63F + side * 0.035F) *
        (0.025F + motion * 0.018F);
    const auto locomotion_swing = -stride * side * motion * 0.10F;
    const glm::vec3 shoulder {
        0.035F,
        2.86F + asymmetry,
        side * 0.48F,
    };
    const glm::vec3 sleeve_end {
        0.055F + reach * 0.19F,
        2.47F + asymmetry,
        side * (0.525F - reach * 0.045F),
    };
    const glm::vec3 elbow {
        0.02F + locomotion_swing + reach * 0.62F,
        1.78F + asymmetry + dead_sway,
        side * (0.60F - reach * 0.11F),
    };
    const glm::vec3 wrist {
        0.035F - locomotion_swing * 0.55F +
            reach * (1.42F + jumpscare * 0.20F),
        0.86F + asymmetry + dead_sway * 1.3F + reach * 0.22F,
        side * (0.565F - reach * 0.16F),
    };

    append_segment(
        parts,
        kBackroomsMarlowVisualPartBudget,
        upper_root,
        shoulder,
        sleeve_end,
        0.175F,
        0.185F,
        0.045F,
        CreatureAtlasTile::MarlowUniform,
        kMaterialSoakedFabric,
        0.47F,
        0.0F,
        context);
    append_segment(
        parts,
        kBackroomsMarlowVisualPartBudget,
        upper_root,
        sleeve_end,
        elbow,
        0.128F,
        0.138F,
        0.045F,
        side < 0.0F
            ? CreatureAtlasTile::MarlowRot
            : CreatureAtlasTile::MarlowSkin,
        side < 0.0F ? kMaterialWetRot : kMaterialWetSkin,
        0.57F,
        0.0F,
        context);
    append_segment(
        parts,
        kBackroomsMarlowVisualPartBudget,
        upper_root,
        elbow,
        wrist,
        0.105F,
        0.115F,
        0.050F,
        CreatureAtlasTile::MarlowSkin,
        kMaterialWetSkin,
        0.61F,
        0.0F,
        context);
    append_box(
        parts,
        kBackroomsMarlowVisualPartBudget,
        upper_root,
        wrist + glm::vec3 {0.035F, -0.115F, 0.0F},
        glm::vec3 {0.13F, 0.19F, 0.175F},
        glm::vec3 {0.0F, side * 0.04F, side * 0.035F},
        CreatureAtlasTile::MarlowRot,
        kMaterialWetRot,
        0.63F,
        0.0F,
        context);

    // Je garde cinq doigts par main, assez longs pour lire la menace a
    // distance mais assez epais pour ne pas devenir des micro-primitives.
    for (int finger = 0; finger < 5; ++finger) {
        const auto finger_value = static_cast<float>(finger);
        const auto spread = (finger_value - 2.0F) * 0.056F;
        const glm::vec3 knuckle =
            wrist + glm::vec3 {0.07F, -0.16F, spread};
        const glm::vec3 tip =
            knuckle +
            glm::vec3 {
                0.015F + reach * (0.20F + 0.018F * finger_value),
                -0.30F - 0.018F * static_cast<float>(finger % 2),
                spread * 0.20F + side * 0.012F,
            };
        append_segment(
            parts,
            kBackroomsMarlowVisualPartBudget,
            upper_root,
            knuckle,
            tip,
            0.028F,
            0.024F,
            0.014F,
            CreatureAtlasTile::MarlowRot,
            kMaterialWetRot,
            0.68F,
            0.0F,
            context);
    }
}

} // namespace

auto backrooms_marlow_visual_body_yaw_radians(
    float gameplay_yaw_degrees) noexcept -> float {
    return periodic_angle(
        kPi * 0.5F -
        finite_or(gameplay_yaw_degrees, 0.0F) *
            kPi / 180.0F);
}

auto backrooms_marlow_visual_head_yaw_radians(
    float gameplay_head_yaw_degrees) noexcept -> float {
    return periodic_angle(
        -finite_or(gameplay_head_yaw_degrees, 0.0F) *
        kPi / 180.0F);
}

auto backrooms_marlow_visual_submersion_ratio(
    float phase_immersion_ratio,
    float reveal_amount) noexcept -> float {
    const auto phase_immersion = smoothstep01(
        finite_or(phase_immersion_ratio, 1.0F));
    const auto reveal = smoothstep01(
        finite_or(reveal_amount, 0.0F));
    return glm::clamp(
        1.0F - (1.0F - phase_immersion) * reveal,
        0.0F,
        1.0F);
}

auto build_backrooms_marlow_visual_parts(
    const BackroomsMarlowVisualPose& source)
    -> std::vector<CreaturePartInstance> {
    const auto animation_time = finite_or(source.animation_time, 0.0F);
    const auto motion = smoothstep01(source.motion_amount);
    const auto submersion = smoothstep01(source.submersion_ratio);
    const auto peek = signed_unit(source.peek_amount);
    const auto jumpscare = source.jumpscare ? 1.0F : 0.0F;
    const auto reach = std::max(
        smoothstep01(source.reach_amount),
        jumpscare);
    const auto cadence = 2.20F + motion * 1.35F + jumpscare * 1.80F;
    const auto stride = wave(animation_time, cadence) * 0.95F;
    const auto body_bob =
        std::abs(wave(animation_time, cadence * 2.0F)) * motion * 0.018F;
    const auto breath = wave(animation_time, 1.08F) * 0.011F;
    const auto body_sway =
        wave(animation_time, 0.54F) * (0.035F + motion * 0.025F);

    MarlowBuildContext context {};
    context.tension = glm::clamp(
        0.88F + reach * 0.07F + jumpscare * 0.05F,
        0.0F,
        1.0F);
    context.sky_light = saturate(source.sky_light);
    context.block_light = saturate(source.block_light);

    auto root = glm::translate(
        glm::mat4 {1.0F},
        finite_position(source.position) -
            glm::vec3 {
                0.0F,
                submersion * kBackroomsMarlowMaximumSubmersion,
                0.0F,
            });
    root = glm::rotate(
        root,
        periodic_angle(source.yaw_radians),
        glm::vec3 {0.0F, 1.0F, 0.0F});

    std::vector<CreaturePartInstance> parts {};
    parts.reserve(kBackroomsMarlowVisualPartBudget);
    append_legs(parts, root, stride, motion, body_bob, context);

    const glm::vec3 upper_pivot {0.0F, 1.86F, 0.0F};
    auto upper_root = glm::translate(root, upper_pivot);
    upper_root = glm::rotate(
        upper_root,
        -0.045F - reach * 0.12F - jumpscare * 0.08F,
        glm::vec3 {0.0F, 0.0F, 1.0F});
    upper_root = glm::rotate(
        upper_root,
        peek * 0.16F,
        glm::vec3 {1.0F, 0.0F, 0.0F});
    upper_root = glm::translate(upper_root, -upper_pivot);
    upper_root = glm::translate(
        upper_root,
        glm::vec3 {
            reach * 0.05F,
            body_bob,
            peek * 0.28F,
        });

    append_torso(parts, upper_root, breath, body_sway, context);
    const auto idle_scan =
        wave(animation_time, 0.31F) *
        (1.0F - motion) * (1.0F - jumpscare) * 0.055F;
    const auto head_scan = glm::clamp(
        finite_or(source.head_scan_radians, 0.0F) + idle_scan,
        -0.72F,
        0.72F);
    append_head(
        parts,
        upper_root,
        animation_time,
        breath,
        head_scan,
        peek,
        reach,
        jumpscare,
        context);
    append_arm(
        parts,
        upper_root,
        -1.0F,
        stride,
        animation_time,
        motion,
        reach,
        jumpscare,
        context);
    append_arm(
        parts,
        upper_root,
        1.0F,
        stride,
        animation_time,
        motion,
        reach,
        jumpscare,
        context);
    return parts;
}

auto build_backrooms_marlow_buoy_visual_parts(
    const BackroomsMarlowBuoyVisualPose& source)
    -> std::vector<CreaturePartInstance> {
    constexpr auto kSegmentCount = 16;
    constexpr auto kRadius = 0.39F;
    constexpr auto kTubeRadius = 0.065F;
    const auto animation_time = finite_or(source.animation_time, 0.0F);
    const auto disturbance = smoothstep01(source.disturbance);
    const auto bob =
        wave(animation_time, 1.65F) * (0.018F + disturbance * 0.028F);
    const auto pitch =
        wave(animation_time, 0.91F, 0.70F) *
        (0.035F + disturbance * 0.055F);
    const auto roll =
        wave(animation_time, 1.13F, 1.40F) *
        (0.028F + disturbance * 0.048F);

    MarlowBuildContext context {};
    context.tension = 0.72F + disturbance * 0.18F;
    context.sky_light = saturate(source.sky_light);
    context.block_light = saturate(source.block_light);

    auto root = glm::translate(
        glm::mat4 {1.0F},
        finite_position(source.water_surface_position) +
            glm::vec3 {0.0F, bob, 0.0F});
    root = glm::rotate(
        root,
        periodic_angle(source.yaw_radians),
        glm::vec3 {0.0F, 1.0F, 0.0F});
    root = glm::rotate(root, pitch, glm::vec3 {1.0F, 0.0F, 0.0F});
    root = glm::rotate(root, roll, glm::vec3 {0.0F, 0.0F, 1.0F});

    std::vector<CreaturePartInstance> parts {};
    parts.reserve(kBackroomsMarlowBuoyVisualPartBudget);
    for (auto segment = 0; segment < kSegmentCount; ++segment) {
        const auto angle0 =
            static_cast<float>(segment) * kTwoPi /
            static_cast<float>(kSegmentCount);
        const auto angle1 =
            static_cast<float>(segment + 1) * kTwoPi /
            static_cast<float>(kSegmentCount);
        const glm::vec3 start {
            std::cos(angle0) * kRadius,
            0.0F,
            std::sin(angle0) * kRadius,
        };
        const glm::vec3 end {
            std::cos(angle1) * kRadius,
            0.0F,
            std::sin(angle1) * kRadius,
        };
        append_segment(
            parts,
            kBackroomsMarlowBuoyVisualPartBudget,
            root,
            start,
            end,
            kTubeRadius,
            kTubeRadius,
            0.020F,
            CreatureAtlasTile::MarlowBuoyYellow,
            kMaterialRubber,
            0.18F,
            0.0F,
            context);
    }
    return parts;
}

} // namespace valcraft
