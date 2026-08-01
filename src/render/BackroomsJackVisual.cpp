#include "render/BackroomsJackVisual.h"

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

constexpr float kMaterialFabric = 0.22F;
constexpr float kMaterialSkin = 0.34F;
constexpr float kMaterialLeather = 0.52F;
constexpr float kMaterialWood = 0.58F;
constexpr float kMaterialClaw = 0.66F;
constexpr float kMaterialBone = 0.88F;
constexpr float kMaterialEye = 0.96F;
constexpr float kMaterialMetal = 0.86F;

struct JackBuildContext {
    float tension = 0.86F;
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

[[nodiscard]] auto smoothstep01(float value) noexcept -> float {
    const auto clamped = saturate(value);
    return clamped * clamped * (3.0F - 2.0F * clamped);
}

[[nodiscard]] auto periodic_angle(float value) noexcept -> float {
    if (!std::isfinite(value)) {
        return 0.0F;
    }
    return static_cast<float>(
        std::remainder(static_cast<double>(value),
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
    const auto rect = BoxUvRect {
        static_cast<float>(tile_x) * kStep,
        static_cast<float>(tile_y) * kStep,
        static_cast<float>(tile_x + 1) * kStep,
        static_cast<float>(tile_y + 1) * kStep,
    };
    std::array<BoxUvRect, 6> result {};
    result.fill(rect);
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
    const glm::mat4& transform,
    CreatureAtlasTile tile,
    float material_class,
    float cavity_mask,
    float emissive_strength,
    const JackBuildContext& context) {
    if (parts.size() >= kBackroomsJackVisualPartBudget) {
        return;
    }

    CreaturePartInstance part {};
    part.transform = transform;
    part.face_uvs = uniform_uvs(tile);
    part.nightmare_factor = 1.0F;
    part.tension = context.tension;
    part.material_class = material_class;
    part.cavity_mask = glm::clamp(cavity_mask, 0.0F, 1.0F);
    part.emissive_strength =
        glm::clamp(emissive_strength, 0.0F, 1.0F);
    part.sky_light = context.sky_light;
    part.block_light = context.block_light;
    part.precipitation_exposure = 0.0F;
    parts.push_back(part);
}

void append_box(
    std::vector<CreaturePartInstance>& parts,
    const glm::mat4& root,
    const glm::vec3& center,
    const glm::vec3& half_extent,
    const glm::vec3& rotation_radians,
    CreatureAtlasTile tile,
    float material_class,
    float cavity_mask,
    float emissive_strength,
    const JackBuildContext& context) {
    if (half_extent.x <= kMinimumPartExtent ||
        half_extent.y <= kMinimumPartExtent ||
        half_extent.z <= kMinimumPartExtent) {
        return;
    }
    append_part(
        parts,
        make_box_transform(root, center, half_extent, rotation_radians),
        tile,
        material_class,
        cavity_mask,
        emissive_strength,
        context);
}

void append_segment(
    std::vector<CreaturePartInstance>& parts,
    const glm::mat4& root,
    const glm::vec3& start,
    const glm::vec3& end,
    float half_width,
    float half_depth,
    float overlap,
    CreatureAtlasTile tile,
    float material_class,
    float cavity_mask,
    float emissive_strength,
    const JackBuildContext& context) {
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
        root * local_transform,
        tile,
        material_class,
        cavity_mask,
        emissive_strength,
        context);
}

void append_pair(
    std::vector<CreaturePartInstance>& parts,
    const glm::mat4& root,
    const glm::vec3& center,
    float side_offset,
    const glm::vec3& half_extent,
    const glm::vec3& rotation,
    const glm::vec3& side_rotation,
    CreatureAtlasTile tile,
    float material_class,
    float cavity_mask,
    float emissive_strength,
    const JackBuildContext& context) {
    for (const auto side : {-1.0F, 1.0F}) {
        append_box(
            parts,
            root,
            center + glm::vec3 {0.0F, 0.0F, side * side_offset},
            half_extent,
            rotation + side_rotation * side,
            tile,
            material_class,
            cavity_mask,
            emissive_strength,
            context);
    }
}

void append_legs(
    std::vector<CreaturePartInstance>& parts,
    const glm::mat4& root,
    float stride,
    float motion,
    float chase,
    float pelvis_bob,
    const JackBuildContext& context) {
    const auto step_scale = motion * (0.13F + chase * 0.09F);
    const auto normal_step_x = stride * step_scale;
    const auto peg_step_x = -stride * step_scale * 0.76F;
    const auto normal_lift =
        std::max(stride, 0.0F) * motion * (0.11F + chase * 0.05F);
    const auto peg_lift =
        std::max(-stride, 0.0F) * motion * (0.055F + chase * 0.025F);

    const glm::vec3 normal_hip {
        -0.02F,
        1.49F + pelvis_bob,
        -0.15F,
    };
    const glm::vec3 normal_knee {
        0.025F + normal_step_x * 0.28F,
        0.82F + normal_lift * 0.30F,
        -0.15F,
    };
    const glm::vec3 normal_ankle {
        normal_step_x,
        0.22F + normal_lift,
        -0.15F,
    };
    append_segment(
        parts,
        root,
        normal_hip,
        normal_knee,
        0.105F,
        0.115F,
        0.025F,
        CreatureAtlasTile::CrewHairBlack,
        kMaterialFabric,
        0.25F,
        0.0F,
        context);
    append_segment(
        parts,
        root,
        normal_knee,
        normal_ankle,
        0.078F,
        0.085F,
        0.025F,
        CreatureAtlasTile::ZombieBone,
        kMaterialBone,
        0.58F,
        0.0F,
        context);
    append_box(
        parts,
        root,
        glm::vec3 {
            normal_ankle.x + 0.085F,
            0.11F + normal_lift,
            normal_ankle.z,
        },
        glm::vec3 {0.19F, 0.11F, 0.135F},
        glm::vec3 {0.0F, 0.0F, normal_step_x * -0.22F},
        CreatureAtlasTile::CrewLeather,
        kMaterialLeather,
        0.18F,
        0.0F,
        context);

    const glm::vec3 peg_hip {
        -0.02F,
        1.49F + pelvis_bob,
        0.15F,
    };
    const glm::vec3 peg_knee {
        -0.015F + peg_step_x * 0.20F,
        0.81F + peg_lift * 0.18F,
        0.15F,
    };
    const glm::vec3 peg_tip {
        peg_step_x,
        0.13F + peg_lift,
        0.15F,
    };
    append_segment(
        parts,
        root,
        peg_hip,
        peg_knee,
        0.105F,
        0.115F,
        0.025F,
        CreatureAtlasTile::CrewHairBlack,
        kMaterialFabric,
        0.27F,
        0.0F,
        context);
    append_segment(
        parts,
        root,
        peg_knee,
        peg_tip,
        0.067F,
        0.067F,
        0.028F,
        CreatureAtlasTile::CrewWood,
        kMaterialWood,
        0.34F,
        0.0F,
        context);
    append_box(
        parts,
        root,
        glm::mix(peg_knee, peg_tip, 0.20F),
        glm::vec3 {0.088F, 0.035F, 0.088F},
        glm::vec3 {0.0F},
        CreatureAtlasTile::CrewIron,
        kMaterialMetal,
        0.12F,
        0.0F,
        context);
    append_box(
        parts,
        root,
        glm::vec3 {peg_tip.x, 0.07F + peg_lift, peg_tip.z},
        glm::vec3 {0.080F, 0.070F, 0.080F},
        glm::vec3 {0.0F},
        CreatureAtlasTile::CrewWood,
        kMaterialWood,
        0.32F,
        0.0F,
        context);
}

void append_torso_and_coat(
    std::vector<CreaturePartInstance>& parts,
    const glm::mat4& upper_root,
    float breath,
    float coat_sway,
    float chase,
    const JackBuildContext& context) {
    append_box(
        parts,
        upper_root,
        glm::vec3 {0.0F, 1.51F, 0.0F},
        glm::vec3 {0.17F, 0.13F, 0.25F},
        glm::vec3 {0.0F},
        CreatureAtlasTile::CrewHairBlack,
        kMaterialFabric,
        0.36F,
        0.0F,
        context);
    append_box(
        parts,
        upper_root,
        glm::vec3 {-0.015F, 1.77F + breath, 0.0F},
        glm::vec3 {0.12F, 0.23F, 0.23F},
        glm::vec3 {0.0F, 0.0F, -0.025F},
        CreatureAtlasTile::ZombieBone,
        kMaterialBone,
        0.70F,
        0.0F,
        context);
    append_box(
        parts,
        upper_root,
        glm::vec3 {0.015F, 2.20F + breath, 0.0F},
        glm::vec3 {0.095F, 0.30F, 0.12F},
        glm::vec3 {0.0F, 0.0F, -0.035F},
        CreatureAtlasTile::ZombieBone,
        kMaterialBone,
        0.78F,
        0.0F,
        context);

    for (int rib = 0; rib < 5; ++rib) {
        const auto index = static_cast<float>(rib);
        const auto y = 1.92F + index * 0.145F + breath;
        const auto span = 0.22F + index * 0.018F;
        append_segment(
            parts,
            upper_root,
            glm::vec3 {0.075F - index * 0.006F, y, -span},
            glm::vec3 {0.095F + index * 0.012F, y + 0.015F, span},
            0.025F,
            0.025F,
            0.008F,
            CreatureAtlasTile::ZombieBone,
            kMaterialBone,
            0.82F,
            0.0F,
            context);
    }

    // Je laisse une chemise claire apparaitre entre les deux revers : la
    // lecture du costume reste nette même lorsque le manteau est très sombre.
    append_box(
        parts,
        upper_root,
        glm::vec3 {0.145F, 2.37F + breath, 0.0F},
        glm::vec3 {0.035F, 0.40F, 0.17F},
        glm::vec3 {0.0F, 0.0F, -0.035F},
        CreatureAtlasTile::CrewIvoryCloth,
        kMaterialFabric,
        0.28F,
        0.0F,
        context);
    append_pair(
        parts,
        upper_root,
        glm::vec3 {0.176F, 2.39F + breath, 0.0F},
        0.145F,
        glm::vec3 {0.030F, 0.41F, 0.11F},
        glm::vec3 {0.0F, 0.0F, -0.08F},
        glm::vec3 {0.0F, 0.14F, 0.045F},
        CreatureAtlasTile::CrewBurgundyCloth,
        kMaterialFabric,
        0.43F,
        0.0F,
        context);
    append_box(
        parts,
        upper_root,
        glm::vec3 {-0.105F, 2.34F + breath, 0.0F},
        glm::vec3 {0.042F, 0.48F, 0.31F},
        glm::vec3 {0.0F, 0.0F, 0.015F},
        CreatureAtlasTile::CrewHairBlack,
        kMaterialFabric,
        0.45F,
        0.0F,
        context);
    append_pair(
        parts,
        upper_root,
        glm::vec3 {0.0F, 2.49F + breath, 0.0F},
        0.31F,
        glm::vec3 {0.14F, 0.37F, 0.050F},
        glm::vec3 {0.0F, 0.0F, -0.045F},
        glm::vec3 {0.05F, 0.0F, 0.065F},
        CreatureAtlasTile::CrewHairBlack,
        kMaterialFabric,
        0.46F,
        0.0F,
        context);

    append_box(
        parts,
        upper_root,
        glm::vec3 {0.015F, 1.61F, 0.0F},
        glm::vec3 {0.205F, 0.065F, 0.31F},
        glm::vec3 {0.0F, 0.0F, 0.02F},
        CreatureAtlasTile::CrewLeather,
        kMaterialLeather,
        0.31F,
        0.0F,
        context);
    append_box(
        parts,
        upper_root,
        glm::vec3 {0.225F, 1.62F, 0.0F},
        glm::vec3 {0.030F, 0.090F, 0.090F},
        glm::vec3 {0.0F, 0.0F, 0.02F},
        CreatureAtlasTile::CrewGold,
        kMaterialMetal,
        0.08F,
        0.03F,
        context);
    append_box(
        parts,
        upper_root,
        glm::vec3 {0.07F, 1.73F, 0.0F},
        glm::vec3 {0.055F, 0.095F, 0.34F},
        glm::vec3 {0.0F, 0.0F, -0.08F},
        CreatureAtlasTile::CrewBurgundyCloth,
        kMaterialFabric,
        0.32F,
        0.0F,
        context);

    constexpr std::array<float, 5> kTailOffsets {{
        -0.26F,
        -0.13F,
        0.0F,
        0.13F,
        0.26F,
    }};
    for (std::size_t index = 0; index < kTailOffsets.size(); ++index) {
        const auto centered_index =
            static_cast<float>(index) -
            static_cast<float>(kTailOffsets.size() - 1U) * 0.5F;
        const auto length =
            0.39F + static_cast<float>((index * 3U) % 4U) * 0.055F;
        append_box(
            parts,
            upper_root,
            glm::vec3 {
                -0.075F +
                    coat_sway *
                        (0.12F +
                         static_cast<float>(index) * 0.015F),
                1.17F - length * 0.12F,
                kTailOffsets[index],
            },
            glm::vec3 {0.045F, length, 0.095F},
            glm::vec3 {
                centered_index * 0.025F,
                0.0F,
                0.025F + coat_sway * (0.16F + chase * 0.08F),
            },
            CreatureAtlasTile::CrewHairBlack,
            kMaterialFabric,
            0.52F,
            0.0F,
            context);
    }
}

void append_head_and_hat(
    std::vector<CreaturePartInstance>& parts,
    const glm::mat4& upper_root,
    float head_scan,
    float animation_time,
    float breath,
    float chase,
    float jumpscare,
    const JackBuildContext& context) {
    const glm::vec3 neck_pivot {0.08F, 2.92F, 0.0F};
    auto head_root = glm::translate(upper_root, neck_pivot);
    head_root = glm::rotate(
        head_root,
        head_scan,
        glm::vec3 {0.0F, 1.0F, 0.0F});
    head_root = glm::rotate(
        head_root,
        -0.075F - jumpscare * 0.10F,
        glm::vec3 {0.0F, 0.0F, 1.0F});
    head_root = glm::translate(head_root, -neck_pivot);
    head_root = glm::translate(
        head_root,
        glm::vec3 {
            jumpscare * 0.23F,
            jumpscare * -0.055F,
            0.0F,
        });

    append_segment(
        parts,
        upper_root,
        glm::vec3 {0.05F, 2.70F + breath, 0.0F},
        glm::vec3 {0.08F, 3.07F + breath, 0.0F},
        0.070F,
        0.065F,
        0.020F,
        CreatureAtlasTile::ZombieBone,
        kMaterialBone,
        0.80F,
        0.0F,
        context);
    append_box(
        parts,
        head_root,
        glm::vec3 {0.08F, 3.29F + breath, 0.0F},
        glm::vec3 {0.245F, 0.275F, 0.245F},
        glm::vec3 {0.0F, 0.0F, -0.045F},
        CreatureAtlasTile::ZombieFlesh,
        kMaterialSkin,
        0.77F,
        0.0F,
        context);
    append_pair(
        parts,
        head_root,
        glm::vec3 {0.18F, 3.30F + breath, 0.0F},
        0.165F,
        glm::vec3 {0.115F, 0.145F, 0.075F},
        glm::vec3 {0.0F, 0.0F, -0.035F},
        glm::vec3 {0.06F, 0.0F, 0.05F},
        CreatureAtlasTile::ZombieBone,
        kMaterialBone,
        0.68F,
        0.0F,
        context);

    const auto jaw_open = jumpscare * 0.095F +
                          chase * 0.018F *
                              (0.5F + 0.5F * wave(animation_time, 8.0F));
    append_box(
        parts,
        head_root,
        glm::vec3 {0.255F, 3.145F - jaw_open * 0.35F + breath, 0.0F},
        glm::vec3 {0.145F, 0.115F + jaw_open * 0.35F, 0.205F},
        glm::vec3 {0.0F, 0.0F, -0.025F - jaw_open * 0.30F},
        CreatureAtlasTile::ZombieFlesh,
        kMaterialSkin,
        0.78F,
        0.0F,
        context);
    append_box(
        parts,
        head_root,
        glm::vec3 {0.392F, 3.205F - jaw_open * 0.32F + breath, 0.0F},
        glm::vec3 {
            0.025F,
            0.090F + jaw_open * 0.48F,
            0.155F,
        },
        glm::vec3 {0.0F, 0.0F, -0.015F},
        CreatureAtlasTile::ZombieMouth,
        kMaterialSkin,
        1.0F,
        0.0F,
        context);
    append_box(
        parts,
        head_root,
        glm::vec3 {0.355F, 3.335F + breath, 0.0F},
        glm::vec3 {0.070F, 0.115F, 0.055F},
        glm::vec3 {0.0F, 0.0F, -0.16F},
        CreatureAtlasTile::ZombieFlesh,
        kMaterialSkin,
        0.63F,
        0.0F,
        context);

    for (const auto side : {-1.0F, 1.0F}) {
        const auto eye_twitch =
            wave(animation_time, 10.5F, side * 1.7F) *
            (0.006F + chase * 0.010F);
        append_box(
            parts,
            head_root,
            glm::vec3 {
                0.350F,
                3.425F + breath + eye_twitch,
                side * 0.125F,
            },
            glm::vec3 {0.050F, 0.063F, 0.063F},
            glm::vec3 {0.0F, 0.0F, -0.04F},
            CreatureAtlasTile::ZombieEye,
            kMaterialEye,
            0.93F,
            0.20F + jumpscare * 0.20F,
            context);
        append_box(
            parts,
            head_root,
            glm::vec3 {
                0.401F,
                3.424F + breath + eye_twitch,
                side * 0.125F,
            },
            glm::vec3 {0.014F, 0.022F, 0.022F},
            glm::vec3 {0.0F},
            CreatureAtlasTile::ZombieScar,
            kMaterialEye,
            1.0F,
            0.03F,
            context);
        append_box(
            parts,
            head_root,
            glm::vec3 {
                0.330F,
                3.505F + breath,
                side * 0.125F,
            },
            glm::vec3 {0.050F, 0.035F, 0.105F},
            glm::vec3 {0.0F, side * 0.05F, side * -0.12F},
            CreatureAtlasTile::ZombieBone,
            kMaterialBone,
            0.83F,
            0.0F,
            context);
    }

    for (int tooth = 0; tooth < 7; ++tooth) {
        const auto offset =
            (static_cast<float>(tooth) - 3.0F) * 0.041F;
        const auto crooked =
            static_cast<float>((tooth * 5) % 4 - 1) * 0.015F;
        append_box(
            parts,
            head_root,
            glm::vec3 {
                0.417F,
                3.255F + crooked + breath,
                offset,
            },
            glm::vec3 {0.024F, 0.037F, 0.014F},
            glm::vec3 {0.0F, 0.0F, crooked * 3.0F},
            CreatureAtlasTile::ZombieTeeth,
            kMaterialClaw,
            0.34F,
            0.0F,
            context);
        append_box(
            parts,
            head_root,
            glm::vec3 {
                0.418F,
                3.160F - jaw_open * 0.68F - crooked + breath,
                offset,
            },
            glm::vec3 {0.024F, 0.038F, 0.014F},
            glm::vec3 {0.0F, 0.0F, kPi + crooked * 2.0F},
            CreatureAtlasTile::ZombieTeeth,
            kMaterialClaw,
            0.34F,
            0.0F,
            context);
    }

    constexpr std::array<glm::vec3, 8> kHairRoots {{
        {-0.10F, 3.48F, -0.21F},
        {-0.15F, 3.37F, -0.25F},
        {-0.17F, 3.24F, -0.24F},
        {-0.10F, 3.12F, -0.20F},
        {-0.10F, 3.48F, 0.21F},
        {-0.15F, 3.37F, 0.25F},
        {-0.17F, 3.24F, 0.24F},
        {-0.10F, 3.12F, 0.20F},
    }};
    for (std::size_t index = 0; index < kHairRoots.size(); ++index) {
        const auto side = index < 4U ? -1.0F : 1.0F;
        const auto sway =
            wave(
                animation_time,
                1.9F + static_cast<float>(index) * 0.07F,
                static_cast<float>(index) * 0.61F) *
            (0.025F + chase * 0.045F);
        const auto length =
            0.42F + static_cast<float>((index * 7U) % 4U) * 0.07F;
        append_segment(
            parts,
            head_root,
            kHairRoots[index],
            kHairRoots[index] +
                glm::vec3 {
                    -0.10F + sway,
                    -length,
                    side * (0.025F + sway * 0.45F),
                },
            0.025F,
            0.018F,
            0.006F,
            CreatureAtlasTile::CrewHairBlack,
            kMaterialFabric,
            0.72F,
            0.0F,
            context);
    }

    // Le tricorne est composé de trois panneaux relevés et d'une calotte
    // centrale. Cette géométrie garde sa silhouette quel que soit le LOD.
    append_box(
        parts,
        head_root,
        glm::vec3 {-0.015F, 3.680F + breath, 0.0F},
        glm::vec3 {0.205F, 0.125F, 0.235F},
        glm::vec3 {0.0F, 0.0F, -0.025F},
        CreatureAtlasTile::CrewLeather,
        kMaterialLeather,
        0.38F,
        0.0F,
        context);
    append_box(
        parts,
        head_root,
        glm::vec3 {0.175F, 3.620F + breath, 0.0F},
        glm::vec3 {0.235F, 0.045F, 0.315F},
        glm::vec3 {0.0F, 0.0F, -0.17F},
        CreatureAtlasTile::CrewHairBlack,
        kMaterialFabric,
        0.47F,
        0.0F,
        context);
    for (const auto side : {-1.0F, 1.0F}) {
        append_box(
            parts,
            head_root,
            glm::vec3 {-0.055F, 3.635F + breath, side * 0.205F},
            glm::vec3 {0.255F, 0.042F, 0.125F},
            glm::vec3 {side * 0.30F, side * 0.11F, 0.13F},
            CreatureAtlasTile::CrewHairBlack,
            kMaterialFabric,
            0.48F,
            0.0F,
            context);
    }
}

void append_arms_and_hands(
    std::vector<CreaturePartInstance>& parts,
    const glm::mat4& upper_root,
    float stride,
    float motion,
    float chase,
    float jumpscare,
    float breath,
    const JackBuildContext& context) {
    for (const auto side : {-1.0F, 1.0F}) {
        const auto opposing_stride = stride * -side;
        const glm::vec3 shoulder {
            0.015F,
            2.75F + breath,
            side * 0.34F,
        };
        const glm::vec3 idle_elbow {
            -0.10F + opposing_stride * motion * 0.09F,
            1.78F + breath * 0.35F,
            side * 0.43F,
        };
        const glm::vec3 idle_wrist {
            0.08F - opposing_stride * motion * 0.12F,
            0.66F + std::abs(stride) * motion * 0.035F,
            side * 0.47F,
        };
        const glm::vec3 chase_elbow {
            0.46F + opposing_stride * 0.13F,
            2.05F,
            side * 0.42F,
        };
        const glm::vec3 chase_wrist {
            0.88F - opposing_stride * 0.15F,
            1.35F,
            side * 0.38F,
        };
        const glm::vec3 scare_elbow {
            0.70F,
            2.42F,
            side * 0.38F,
        };
        const glm::vec3 scare_wrist {
            1.30F,
            2.23F,
            side * 0.27F,
        };

        const auto elbow = glm::mix(
            glm::mix(idle_elbow, chase_elbow, chase),
            scare_elbow,
            jumpscare);
        const auto wrist = glm::mix(
            glm::mix(idle_wrist, chase_wrist, chase),
            scare_wrist,
            jumpscare);

        append_box(
            parts,
            upper_root,
            shoulder,
            glm::vec3 {0.115F, 0.125F, 0.095F},
            glm::vec3 {0.0F, 0.0F, side * -0.13F},
            CreatureAtlasTile::CrewHairBlack,
            kMaterialFabric,
            0.45F,
            0.0F,
            context);
        append_segment(
            parts,
            upper_root,
            shoulder,
            elbow,
            0.062F,
            0.067F,
            0.035F,
            CreatureAtlasTile::CrewHairBlack,
            kMaterialFabric,
            0.51F,
            0.0F,
            context);
        append_box(
            parts,
            upper_root,
            elbow,
            glm::vec3 {0.075F, 0.075F, 0.075F},
            glm::vec3 {0.0F},
            CreatureAtlasTile::ZombieBone,
            kMaterialBone,
            0.61F,
            0.0F,
            context);
        append_segment(
            parts,
            upper_root,
            elbow,
            wrist,
            0.048F,
            0.052F,
            0.030F,
            CreatureAtlasTile::ZombieBone,
            kMaterialBone,
            0.70F,
            0.0F,
            context);
        append_box(
            parts,
            upper_root,
            wrist,
            glm::vec3 {0.080F, 0.095F, 0.100F},
            glm::vec3 {0.0F, 0.0F, -0.08F},
            CreatureAtlasTile::ZombieClaw,
            kMaterialClaw,
            0.58F,
            0.0F,
            context);

        const auto reach =
            glm::normalize(
                glm::mix(
                    glm::vec3 {0.11F, -0.99F, 0.0F},
                    glm::vec3 {0.98F, -0.20F, 0.0F},
                    std::max(chase * 0.70F, jumpscare)));
        for (int finger = 0; finger < 5; ++finger) {
            const auto finger_index = static_cast<float>(finger);
            const auto spread = (finger_index - 2.0F) * 0.044F;
            const auto length =
                0.31F + static_cast<float>((finger * 3) % 4) * 0.025F;
            const auto base = wrist +
                              glm::vec3 {
                                  0.025F,
                                  -0.070F,
                                  spread,
                              };
            const auto knuckle =
                base + reach * (length * 0.48F) +
                glm::vec3 {
                    0.0F,
                    0.0F,
                    side * spread * 0.08F,
                };
            const auto tip =
                base + reach * length +
                glm::vec3 {
                    0.015F * finger_index,
                    -0.012F * static_cast<float>(finger % 2),
                    side * spread * 0.16F,
                };
            append_segment(
                parts,
                upper_root,
                base,
                knuckle,
                0.021F,
                0.018F,
                0.010F,
                CreatureAtlasTile::ZombieClaw,
                kMaterialClaw,
                0.49F,
                0.0F,
                context);
            append_segment(
                parts,
                upper_root,
                knuckle,
                tip,
                0.017F,
                0.014F,
                0.008F,
                CreatureAtlasTile::ZombieClaw,
                kMaterialClaw,
                0.53F,
                0.0F,
                context);

            const auto fork_direction =
                glm::normalize(
                    reach +
                    glm::vec3 {0.06F, -0.04F, side * 0.10F});
            for (const auto fork_side : {-1.0F, 1.0F}) {
                append_segment(
                    parts,
                    upper_root,
                    tip,
                    tip + fork_direction * 0.105F +
                        glm::vec3 {0.0F, 0.0F, fork_side * 0.026F},
                    0.011F,
                    0.009F,
                    0.004F,
                    CreatureAtlasTile::ZombieClaw,
                    kMaterialClaw,
                    0.56F,
                    0.0F,
                    context);
            }
        }
    }
}

} // namespace

auto backrooms_jack_visual_body_yaw_radians(
    float gameplay_yaw_degrees) noexcept -> float {
    return periodic_angle(
        kPi * 0.5F -
        finite_or(gameplay_yaw_degrees, 0.0F) *
            kPi / 180.0F);
}

auto backrooms_jack_visual_head_yaw_radians(
    float gameplay_head_yaw_degrees) noexcept -> float {
    return periodic_angle(
        -finite_or(gameplay_head_yaw_degrees, 0.0F) *
        kPi / 180.0F);
}

auto build_backrooms_jack_visual_parts(
    const BackroomsJackVisualPose& source)
    -> std::vector<CreaturePartInstance> {
    const auto hunch = smoothstep01(source.hunch_ratio);
    const auto motion = smoothstep01(source.motion_amount);
    const auto chase = source.chasing ? 1.0F : 0.0F;
    const auto jumpscare = source.jumpscare ? 1.0F : 0.0F;
    const auto animation_time = finite_or(source.animation_time, 0.0F);
    const auto cadence = 3.15F + chase * 2.75F;
    const auto stride = wave(animation_time, cadence) * motion;
    const auto pelvis_bob =
        std::abs(wave(animation_time, cadence * 2.0F)) *
        motion * (0.018F + chase * 0.018F);
    const auto breath =
        wave(animation_time, 1.45F) * (0.009F + chase * 0.004F);
    const auto coat_sway =
        wave(animation_time, cadence, kPi * 0.25F) *
        motion * (0.08F + chase * 0.08F);

    JackBuildContext context {};
    context.tension = glm::clamp(
        0.84F + chase * 0.10F + jumpscare * 0.06F,
        0.0F,
        1.0F);
    context.sky_light = saturate(source.sky_light);
    context.block_light = saturate(source.block_light);

    auto root = glm::translate(
        glm::mat4 {1.0F},
        finite_position(source.position));
    root = glm::rotate(
        root,
        periodic_angle(source.yaw_radians),
        glm::vec3 {0.0F, 1.0F, 0.0F});

    std::vector<CreaturePartInstance> parts {};
    parts.reserve(kBackroomsJackVisualPartBudget);

    append_legs(
        parts,
        root,
        stride,
        motion,
        chase,
        pelvis_bob,
        context);

    auto upper_root = glm::translate(
        root,
        glm::vec3 {
            jumpscare * 0.08F,
            pelvis_bob,
            0.0F,
        });
    const glm::vec3 hunch_pivot {0.0F, 1.46F, 0.0F};
    upper_root = glm::translate(upper_root, hunch_pivot);
    upper_root = glm::rotate(
        upper_root,
        -hunch * kBackroomsJackVisualMaximumHunchRadians -
            jumpscare * 0.08F,
        glm::vec3 {0.0F, 0.0F, 1.0F});
    upper_root = glm::translate(upper_root, -hunch_pivot);

    append_torso_and_coat(
        parts,
        upper_root,
        breath,
        coat_sway,
        chase,
        context);

    const auto idle_scan =
        wave(animation_time, 0.72F) *
        (1.0F - motion) * (1.0F - jumpscare) * 0.075F;
    const auto head_scan = glm::clamp(
        finite_or(source.head_scan_radians, 0.0F) + idle_scan,
        -0.68F,
        0.68F);
    append_head_and_hat(
        parts,
        upper_root,
        head_scan,
        animation_time,
        breath,
        chase,
        jumpscare,
        context);
    append_arms_and_hands(
        parts,
        upper_root,
        stride,
        motion,
        chase,
        jumpscare,
        breath,
        context);

    return parts;
}

} // namespace valcraft
