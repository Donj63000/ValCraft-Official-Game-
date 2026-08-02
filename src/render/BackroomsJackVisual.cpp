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
constexpr float kOriginalJackStandingHeight = 3.82F;
constexpr float kJackVerticalScale =
    kBackroomsJackVisualStandingHeight /
    kOriginalJackStandingHeight;

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
        CreatureAtlasTile::JackCloth,
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
        CreatureAtlasTile::JackBone,
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
        CreatureAtlasTile::JackCloth,
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
        CreatureAtlasTile::JackCloth,
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
        glm::vec3 {-0.025F, 1.51F, 0.025F},
        glm::vec3 {0.19F, 0.15F, 0.29F},
        glm::vec3 {0.0F, 0.0F, -0.035F},
        CreatureAtlasTile::JackCloth,
        kMaterialFabric,
        0.72F,
        0.0F,
        context);
    append_box(
        parts,
        upper_root,
        glm::vec3 {-0.020F, 1.84F + breath, -0.015F},
        glm::vec3 {0.14F, 0.25F, 0.27F},
        glm::vec3 {0.0F, 0.0F, -0.055F},
        CreatureAtlasTile::JackTar,
        kMaterialSkin,
        0.96F,
        0.0F,
        context);
    append_box(
        parts,
        upper_root,
        glm::vec3 {-0.080F, 2.23F + breath, 0.035F},
        glm::vec3 {0.105F, 0.40F, 0.115F},
        glm::vec3 {0.0F, 0.0F, -0.065F},
        CreatureAtlasTile::JackBone,
        kMaterialBone,
        0.91F,
        0.0F,
        context);

    // Je casse la vieille grille symetrique du torse en deux demi-cages
    // decalees. Les cotes dessinent maintenant un volume creux et malade.
    for (int rib = 0; rib < 5; ++rib) {
        const auto index = static_cast<float>(rib);
        const auto y = 1.93F + index * 0.145F + breath;
        const auto span = 0.235F + index * 0.020F;
        for (const auto side : {-1.0F, 1.0F}) {
            const auto side_bias = side < 0.0F ? -0.012F : 0.018F;
            append_segment(
                parts,
                upper_root,
                glm::vec3 {
                    0.115F + side_bias,
                    y + side * 0.006F,
                    side * 0.025F,
                },
                glm::vec3 {
                    0.015F - index * 0.010F,
                    y + 0.030F + side * 0.010F,
                    side * span,
                },
                0.028F,
                0.026F,
                0.010F,
                CreatureAtlasTile::JackBone,
                kMaterialBone,
                0.90F,
                0.0F,
                context);
        }
    }
    append_segment(
        parts,
        upper_root,
        glm::vec3 {0.115F, 1.82F + breath, -0.018F},
        glm::vec3 {0.135F, 2.61F + breath, -0.050F},
        0.034F,
        0.030F,
        0.012F,
        CreatureAtlasTile::JackBone,
        kMaterialBone,
        0.88F,
        0.0F,
        context);

    // Je donne un poids different aux deux revers : cette dissymetrie reste
    // visible meme lorsque Jack n'est plus qu'une silhouette au fond du hall.
    for (const auto side : {-1.0F, 1.0F}) {
        const auto heavy_side = side > 0.0F ? 1.0F : 0.0F;
        append_box(
            parts,
            upper_root,
            glm::vec3 {
                0.168F + heavy_side * 0.018F,
                2.38F + breath - heavy_side * 0.055F,
                side * (0.155F + heavy_side * 0.018F),
            },
            glm::vec3 {
                0.030F,
                0.36F + heavy_side * 0.085F,
                0.105F + heavy_side * 0.025F,
            },
            glm::vec3 {
                side * 0.025F,
                side * (0.11F + heavy_side * 0.045F),
                -0.095F + side * 0.035F,
            },
            CreatureAtlasTile::JackCloth,
            kMaterialFabric,
            0.78F,
            0.0F,
            context);
    }
    append_box(
        parts,
        upper_root,
        glm::vec3 {-0.130F, 2.34F + breath, 0.025F},
        glm::vec3 {0.035F, 0.52F, 0.35F},
        glm::vec3 {0.0F, 0.0F, 0.035F},
        CreatureAtlasTile::JackCloth,
        kMaterialFabric,
        0.82F,
        0.0F,
        context);
    for (const auto side : {-1.0F, 1.0F}) {
        const auto shoulder_drop = side > 0.0F ? 0.075F : 0.0F;
        append_box(
            parts,
            upper_root,
            glm::vec3 {
                -0.010F,
                2.72F + breath - shoulder_drop,
                side * (0.34F + shoulder_drop * 0.20F),
            },
            glm::vec3 {
                0.155F,
                0.17F + shoulder_drop * 0.25F,
                0.145F,
            },
            glm::vec3 {
                side * 0.08F,
                side * 0.035F,
                side * -0.16F,
            },
            CreatureAtlasTile::JackCloth,
            kMaterialFabric,
            0.79F,
            0.0F,
            context);
    }

    append_box(
        parts,
        upper_root,
        glm::vec3 {0.010F, 1.61F, 0.020F},
        glm::vec3 {0.215F, 0.065F, 0.33F},
        glm::vec3 {0.0F, 0.0F, 0.035F},
        CreatureAtlasTile::JackCloth,
        kMaterialLeather,
        0.68F,
        0.0F,
        context);
    append_box(
        parts,
        upper_root,
        glm::vec3 {0.232F, 1.62F, -0.035F},
        glm::vec3 {0.024F, 0.070F, 0.072F},
        glm::vec3 {0.0F, 0.0F, 0.04F},
        CreatureAtlasTile::JackBone,
        kMaterialMetal,
        0.44F,
        0.0F,
        context);
    append_box(
        parts,
        upper_root,
        glm::vec3 {0.060F, 1.74F, -0.015F},
        glm::vec3 {0.050F, 0.095F, 0.35F},
        glm::vec3 {0.0F, 0.0F, -0.11F},
        CreatureAtlasTile::JackCloth,
        kMaterialFabric,
        0.72F,
        0.0F,
        context);

    constexpr std::array<float, 3> kTailOffsets {{
        -0.235F,
        0.015F,
        0.265F,
    }};
    constexpr std::array<float, 3> kTailLengths {{
        0.54F,
        0.69F,
        0.48F,
    }};
    for (std::size_t index = 0; index < kTailOffsets.size(); ++index) {
        const auto centered_index =
            static_cast<float>(index) -
            static_cast<float>(kTailOffsets.size() - 1U) * 0.5F;
        const auto length = kTailLengths[index];
        append_box(
            parts,
            upper_root,
            glm::vec3 {
                -0.105F +
                    coat_sway *
                        (0.14F +
                         static_cast<float>(index) * 0.022F),
                1.14F - length * 0.10F,
                kTailOffsets[index],
            },
            glm::vec3 {
                0.026F,
                length,
                index == 1U ? 0.145F : 0.135F,
            },
            glm::vec3 {
                centered_index * 0.055F,
                centered_index * 0.035F,
                0.04F +
                    centered_index * 0.030F +
                    coat_sway * (0.18F + chase * 0.10F),
            },
            CreatureAtlasTile::JackCloth,
            kMaterialFabric,
            0.86F,
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
        -0.135F -
            jumpscare * 0.12F +
            wave(animation_time, 0.43F, 1.1F) *
                (0.018F + chase * 0.010F),
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
        CreatureAtlasTile::JackBone,
        kMaterialBone,
        0.92F,
        0.0F,
        context);
    append_box(
        parts,
        head_root,
        glm::vec3 {0.075F, 3.275F + breath, -0.018F},
        glm::vec3 {0.285F, 0.305F, 0.275F},
        glm::vec3 {0.0F, 0.0F, -0.075F},
        CreatureAtlasTile::JackTar,
        kMaterialSkin,
        0.94F,
        0.0F,
        context);
    for (const auto side : {-1.0F, 1.0F}) {
        const auto sunken_side = side > 0.0F ? 1.0F : 0.0F;
        append_box(
            parts,
            head_root,
            glm::vec3 {
                0.185F,
                3.285F + breath - sunken_side * 0.035F,
                side * (0.175F + sunken_side * 0.010F),
            },
            glm::vec3 {
                0.125F,
                0.150F - sunken_side * 0.018F,
                0.078F,
            },
            glm::vec3 {
                side * 0.065F,
                side * 0.025F,
                -0.055F + side * 0.045F,
            },
            CreatureAtlasTile::JackBone,
            kMaterialBone,
            0.88F,
            0.0F,
            context);
    }

    const auto jaw_open =
        0.032F +
        jumpscare * 0.145F +
        chase *
            (0.055F +
             0.022F * (0.5F + 0.5F * wave(animation_time, 7.3F)));
    append_box(
        parts,
        head_root,
        glm::vec3 {0.270F, 3.125F - jaw_open * 0.42F + breath, 0.018F},
        glm::vec3 {0.165F, 0.125F + jaw_open * 0.38F, 0.225F},
        glm::vec3 {0.0F, 0.0F, -0.055F - jaw_open * 0.38F},
        CreatureAtlasTile::JackTar,
        kMaterialSkin,
        0.96F,
        0.0F,
        context);
    append_box(
        parts,
        head_root,
        glm::vec3 {0.425F, 3.190F - jaw_open * 0.40F + breath, 0.018F},
        glm::vec3 {
            0.025F,
            0.090F + jaw_open * 0.48F,
            0.175F,
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
        glm::vec3 {0.370F, 3.335F + breath, -0.015F},
        glm::vec3 {0.075F, 0.125F, 0.060F},
        glm::vec3 {0.0F, 0.0F, -0.16F},
        CreatureAtlasTile::JackTar,
        kMaterialSkin,
        0.90F,
        0.0F,
        context);

    for (const auto side : {-1.0F, 1.0F}) {
        const auto sunken_side = side > 0.0F ? 1.0F : 0.0F;
        const auto eye_twitch =
            wave(animation_time, 10.5F, side * 1.7F) *
            (0.009F + chase * 0.014F);
        append_box(
            parts,
            head_root,
            glm::vec3 {
                0.365F,
                3.420F + breath + eye_twitch - sunken_side * 0.030F,
                side * (0.132F + sunken_side * 0.008F),
            },
            glm::vec3 {
                0.064F,
                0.082F + sunken_side * 0.006F,
                0.082F,
            },
            glm::vec3 {0.0F, side * 0.035F, -0.06F + side * 0.025F},
            CreatureAtlasTile::JackCloth,
            kMaterialSkin,
            1.0F,
            0.0F,
            context);
        append_box(
            parts,
            head_root,
            glm::vec3 {
                0.427F,
                3.420F + breath + eye_twitch - sunken_side * 0.030F,
                side * (0.132F + sunken_side * 0.008F),
            },
            glm::vec3 {
                0.022F,
                0.043F + sunken_side * 0.007F,
                0.043F + sunken_side * 0.007F,
            },
            glm::vec3 {0.0F, side * 0.020F, -0.04F},
            CreatureAtlasTile::JackEye,
            kMaterialEye,
            1.0F,
            0.76F +
                (side < 0.0F ? 0.10F : 0.0F) +
                jumpscare * 0.14F,
            context);
        append_box(
            parts,
            head_root,
            glm::vec3 {
                0.335F,
                3.520F + breath - sunken_side * 0.020F,
                side * (0.132F + sunken_side * 0.008F),
            },
            glm::vec3 {
                0.055F,
                0.038F,
                0.115F + sunken_side * 0.012F,
            },
            glm::vec3 {0.0F, side * 0.065F, side * -0.18F},
            CreatureAtlasTile::JackBone,
            kMaterialBone,
            0.94F,
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
            CreatureAtlasTile::JackCloth,
            kMaterialFabric,
            0.88F,
            0.0F,
            context);
    }

    // Le tricorne est composé de trois panneaux relevés et d'une calotte
    // centrale. Cette géométrie garde sa silhouette quel que soit le LOD.
    append_box(
        parts,
        head_root,
        glm::vec3 {-0.030F, 3.675F + breath, 0.025F},
        glm::vec3 {0.225F, 0.125F, 0.275F},
        glm::vec3 {0.0F, 0.0F, -0.055F},
        CreatureAtlasTile::JackCloth,
        kMaterialLeather,
        0.82F,
        0.0F,
        context);
    append_box(
        parts,
        head_root,
        glm::vec3 {0.155F, 3.615F + breath, -0.025F},
        glm::vec3 {0.255F, 0.040F, 0.405F},
        glm::vec3 {0.0F, -0.055F, -0.21F},
        CreatureAtlasTile::JackCloth,
        kMaterialFabric,
        0.86F,
        0.0F,
        context);
    for (const auto side : {-1.0F, 1.0F}) {
        const auto torn_side = side > 0.0F ? 1.0F : 0.0F;
        append_box(
            parts,
            head_root,
            glm::vec3 {
                -0.075F + torn_side * 0.025F,
                3.625F + breath - torn_side * 0.030F,
                side * (0.265F + torn_side * 0.018F),
            },
            glm::vec3 {
                0.285F - torn_side * 0.040F,
                0.038F,
                0.145F + torn_side * 0.020F,
            },
            glm::vec3 {
                side * (0.34F + torn_side * 0.08F),
                side * (0.14F - torn_side * 0.05F),
                0.15F + side * 0.035F,
            },
            CreatureAtlasTile::JackCloth,
            kMaterialFabric,
            0.88F,
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
    float animation_time,
    const JackBuildContext& context) {
    for (const auto side : {-1.0F, 1.0F}) {
        const auto heavy_side = side > 0.0F ? 1.0F : 0.0F;
        const auto phase = side < 0.0F ? 0.35F : 2.10F;
        const auto slow_curl =
            wave(
                animation_time,
                0.82F + chase * 1.65F,
                phase);
        const auto living_sway =
            wave(
                animation_time,
                1.37F + motion * 1.60F,
                phase * 1.7F);
        const glm::vec3 shoulder {
            -0.015F,
            2.74F + breath - heavy_side * 0.075F,
            side * (0.37F + heavy_side * 0.018F),
        };

        // Je construis chaque bras comme une chaine continue. Les deux courbes
        // n'ont ni la meme longueur ni la meme phase, ce qui retire toute
        // lecture de mannequin articule lorsque Jack reste immobile.
        std::array<glm::vec3, 6> idle_points {{
            shoulder,
            {-0.17F, 2.30F, side * 0.48F},
            {0.06F, 1.86F - heavy_side * 0.035F,
             side * (0.57F + heavy_side * 0.025F)},
            {-0.035F, 1.40F - heavy_side * 0.060F,
             side * (0.51F + heavy_side * 0.055F)},
            {0.19F, 0.96F - heavy_side * 0.070F,
             side * (0.59F + heavy_side * 0.040F)},
            {0.10F + heavy_side * 0.075F,
             0.62F + heavy_side * 0.085F,
             side * (0.50F + heavy_side * 0.105F)},
        }};
        std::array<glm::vec3, 6> chase_points {{
            shoulder,
            {0.42F - stride * side * 0.08F, 2.43F, side * 0.45F},
            {0.82F + stride * side * 0.06F, 2.05F, side * 0.39F},
            {1.17F - stride * side * 0.08F, 1.68F, side * 0.32F},
            {1.49F + stride * side * 0.07F, 1.39F, side * 0.25F},
            {1.76F - heavy_side * 0.06F, 1.22F, side * 0.18F},
        }};
        const std::array<glm::vec3, 6> scare_points {{
            shoulder,
            {0.55F, 2.59F, side * 0.40F},
            {1.04F, 2.44F, side * 0.31F},
            {1.47F, 2.31F, side * 0.22F},
            {1.84F, 2.22F, side * 0.14F},
            {2.14F - heavy_side * 0.05F, 2.15F, side * 0.07F},
        }};

        std::array<glm::vec3, 6> points {};
        points.front() = shoulder;
        for (std::size_t index = 1U; index < points.size(); ++index) {
            const auto along =
                static_cast<float>(index) /
                static_cast<float>(points.size() - 1U);
            points[index] = glm::mix(
                glm::mix(idle_points[index], chase_points[index], chase),
                scare_points[index],
                jumpscare);
            const auto curl =
                wave(
                    animation_time,
                    1.75F + chase * 2.40F,
                    phase + static_cast<float>(index) * 0.72F);
            points[index].x +=
                (slow_curl * 0.055F + curl * 0.040F) *
                along * (1.0F - jumpscare * 0.65F);
            points[index].y +=
                living_sway * 0.020F * along *
                (0.55F + motion * 0.45F);
            points[index].z +=
                side * curl * 0.045F * along *
                (1.0F + chase * 0.45F);
        }

        append_box(
            parts,
            upper_root,
            shoulder,
            glm::vec3 {0.145F, 0.155F, 0.125F},
            glm::vec3 {side * 0.05F, 0.0F, side * -0.18F},
            CreatureAtlasTile::JackCloth,
            kMaterialFabric,
            0.82F,
            0.0F,
            context);
        constexpr std::array<float, 5> kTentacleWidths {{
            0.125F,
            0.108F,
            0.091F,
            0.074F,
            0.059F,
        }};
        for (std::size_t index = 0U;
             index + 1U < points.size();
             ++index) {
            append_segment(
                parts,
                upper_root,
                points[index],
                points[index + 1U],
                kTentacleWidths[index],
                kTentacleWidths[index] * 0.86F,
                0.040F,
                CreatureAtlasTile::JackTar,
                kMaterialSkin,
                0.82F + static_cast<float>(index) * 0.030F,
                0.0F,
                context);
        }

        const auto palm = points.back();
        append_box(
            parts,
            upper_root,
            palm,
            glm::vec3 {0.105F, 0.115F, 0.125F},
            glm::vec3 {side * 0.08F, side * 0.035F, -0.12F},
            CreatureAtlasTile::JackTar,
            kMaterialSkin,
            0.91F,
            0.0F,
            context);

        const auto attack_reach =
            std::max(chase * 0.88F, jumpscare);
        const auto reach =
            glm::normalize(
                glm::mix(
                    glm::vec3 {0.10F, -0.99F, side * 0.035F},
                    glm::vec3 {0.98F, -0.16F, side * -0.08F},
                    attack_reach));
        for (int claw = 0; claw < 3; ++claw) {
            const auto claw_index = static_cast<float>(claw);
            const auto spread = (claw_index - 1.0F) * 0.105F;
            const auto length =
                0.36F +
                static_cast<float>((claw * 2 + (side > 0.0F ? 1 : 0)) % 3) *
                    0.045F;
            const auto base =
                palm + glm::vec3 {0.025F, -0.045F, spread};
            const auto knuckle =
                base + reach * (length * 0.58F) +
                glm::vec3 {
                    0.0F,
                    0.0F,
                    side * spread * 0.12F,
                };
            const auto hook_direction =
                glm::normalize(
                    reach +
                    glm::vec3 {
                        0.10F,
                        -0.30F + attack_reach * 0.20F,
                        side * 0.10F + spread * 0.22F,
                    });
            const auto tip =
                knuckle + hook_direction * (length * 0.48F) +
                glm::vec3 {0.0F, -0.025F, spread * 0.08F};
            append_segment(
                parts,
                upper_root,
                base,
                knuckle,
                0.034F,
                0.029F,
                0.018F,
                CreatureAtlasTile::ZombieClaw,
                kMaterialClaw,
                0.72F,
                0.0F,
                context);
            append_segment(
                parts,
                upper_root,
                knuckle,
                tip,
                0.026F,
                0.021F,
                0.014F,
                CreatureAtlasTile::ZombieClaw,
                kMaterialClaw,
                0.80F,
                0.0F,
                context);
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
        0.92F + chase * 0.06F + jumpscare * 0.02F,
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
    // Je grandis Jack dans son repere local : sa translation gameplay reste
    // exacte, ses pieds restent poses et sa silhouette atteint environ 4,5 m.
    root = glm::scale(
        root,
        glm::vec3 {1.08F, kJackVerticalScale, 1.10F});

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
        animation_time,
        context);

    return parts;
}

} // namespace valcraft
