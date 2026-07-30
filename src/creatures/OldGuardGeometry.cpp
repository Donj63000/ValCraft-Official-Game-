#include "creatures/OldGuardGeometry.h"

#include "creatures/HumanoidVisualContinuity.h"
#include "creatures/OldGuardAnimation.h"
#include "render/MusketVisualRecipe.h"

#include <glm/common.hpp>
#include <glm/geometric.hpp>
#include <glm/gtc/matrix_inverse.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/vec4.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>

namespace valcraft {

namespace {

constexpr float kMaterialFabric = 0.22F;
constexpr float kMaterialSkin = 0.34F;
constexpr float kMaterialLeather = 0.52F;
constexpr float kMaterialWood = 0.58F;
constexpr float kMaterialMetal = 0.86F;

auto make_uniform_uvs(CreatureAtlasTile tile) -> std::array<BoxUvRect, 6> {
    const auto coordinates = creature_atlas_tile_coordinates(tile);
    const auto step = 1.0F / kCreatureAtlasTilesPerAxis;
    const auto rectangle = BoxUvRect {
        static_cast<float>(coordinates[0]) * step,
        static_cast<float>(coordinates[1]) * step,
        static_cast<float>(coordinates[0] + 1) * step,
        static_cast<float>(coordinates[1] + 1) * step,
    };
    std::array<BoxUvRect, 6> result {};
    result.fill(rectangle);
    return result;
}

auto safe_direction(const glm::vec3& direction, const glm::vec3& fallback) noexcept
    -> glm::vec3 {
    const auto length_squared = glm::dot(direction, direction);
    if (!std::isfinite(length_squared) || length_squared <= 1.0e-8F) {
        return fallback;
    }
    return direction / std::sqrt(length_squared);
}

auto make_box_transform(const glm::mat4& root,
                        const glm::vec3& center,
                        const glm::vec3& half_extent,
                        const glm::vec3& rotation) noexcept -> glm::mat4 {
    auto transform = glm::translate(root, center);
    transform = glm::rotate(
        transform,
        rotation.y,
        glm::vec3 {0.0F, 1.0F, 0.0F});
    transform = glm::rotate(
        transform,
        rotation.z,
        glm::vec3 {0.0F, 0.0F, 1.0F});
    transform = glm::rotate(
        transform,
        rotation.x,
        glm::vec3 {1.0F, 0.0F, 0.0F});
    return glm::scale(transform, half_extent * 2.0F);
}

auto make_segment_transform(const glm::vec3& start,
                            const glm::vec3& end,
                            const glm::vec2& half_width) noexcept -> glm::mat4 {
    const auto delta = end - start;
    const auto length = glm::length(delta);
    const auto axis_x = safe_direction(
        delta,
        glm::vec3 {1.0F, 0.0F, 0.0F});
    auto axis_z = glm::cross(axis_x, glm::vec3 {0.0F, 1.0F, 0.0F});
    if (glm::dot(axis_z, axis_z) <= 1.0e-7F) {
        axis_z = glm::cross(axis_x, glm::vec3 {0.0F, 0.0F, 1.0F});
    }
    axis_z = safe_direction(
        axis_z,
        glm::vec3 {0.0F, 0.0F, 1.0F});
    const auto axis_y =
        safe_direction(
            glm::cross(axis_z, axis_x),
            glm::vec3 {0.0F, 1.0F, 0.0F});

    glm::mat4 transform {1.0F};
    transform[0] = glm::vec4 {axis_x * length, 0.0F};
    transform[1] = glm::vec4 {axis_y * (half_width.x * 2.0F), 0.0F};
    transform[2] = glm::vec4 {axis_z * (half_width.y * 2.0F), 0.0F};
    transform[3] = glm::vec4 {(start + end) * 0.5F, 1.0F};
    return transform;
}

void append_part(std::vector<CreaturePartInstance>& parts,
                 std::size_t first_part,
                 const glm::mat4& transform,
                 CreatureAtlasTile tile,
                 float material,
                 float cavity = 0.08F,
                 float emissive = 0.0F) {
    if (parts.size() - first_part >= kOldGuardVisualPartBudget) {
        return;
    }
    parts.push_back({
        transform,
        make_uniform_uvs(tile),
        0.0F,
        0.0F,
        material,
        cavity,
        emissive,
    });
}

void append_box(std::vector<CreaturePartInstance>& parts,
                std::size_t first_part,
                const glm::mat4& root,
                const glm::vec3& center,
                const glm::vec3& half_extent,
                const glm::vec3& rotation,
                CreatureAtlasTile tile,
                float material,
                float cavity = 0.08F) {
    if (half_extent.x <= 1.0e-4F ||
        half_extent.y <= 1.0e-4F ||
        half_extent.z <= 1.0e-4F) {
        return;
    }
    append_part(
        parts,
        first_part,
        make_box_transform(root, center, half_extent, rotation),
        tile,
        material,
        cavity);
}

void append_segment(std::vector<CreaturePartInstance>& parts,
                    std::size_t first_part,
                    const glm::vec3& start,
                    const glm::vec3& end,
                    const glm::vec2& half_width,
                    CreatureAtlasTile tile,
                    float material,
                    float cavity = 0.08F) {
    const auto distance = glm::length(end - start);
    if (!std::isfinite(distance) ||
        distance <= 1.0e-4F ||
        half_width.x <= 1.0e-4F ||
        half_width.y <= 1.0e-4F) {
        return;
    }
    append_part(
        parts,
        first_part,
        make_segment_transform(start, end, half_width),
        tile,
        material,
        cavity);
}

void append_limb_segment(
    std::vector<CreaturePartInstance>& parts,
    std::size_t first_part,
    const glm::vec3& start,
    const glm::vec3& end,
    const glm::vec2& half_width,
    CreatureAtlasTile tile,
    float material,
    float cavity = 0.08F) {
    const auto span = make_overlapping_humanoid_limb_span(
        start,
        end,
        std::max(half_width.x, half_width.y) * 0.68F);
    if (!span.valid) {
        return;
    }
    append_segment(
        parts,
        first_part,
        span.start,
        span.end,
        half_width,
        tile,
        material,
        cavity);
}

void append_body_space_box(
    std::vector<CreaturePartInstance>& parts,
    std::size_t first_part,
    const glm::mat4& body_root,
    const glm::mat4& world_to_body,
    const glm::vec3& world_center,
    const glm::vec3& local_half_extent,
    CreatureAtlasTile tile,
    float material,
    float cavity = 0.08F) {
    const auto local_center = glm::vec3 {
        world_to_body *
        glm::vec4 {world_center, 1.0F},
    };
    append_box(
        parts,
        first_part,
        body_root,
        local_center,
        local_half_extent,
        glm::vec3 {0.0F},
        tile,
        material,
        cavity);
}

auto skin_tile(std::uint32_t seed) noexcept -> CreatureAtlasTile {
    switch ((seed >> 4U) % 3U) {
    case 0U:
        return CreatureAtlasTile::CrewSkinLight;
    case 1U:
        return CreatureAtlasTile::CrewSkinMedium;
    case 2U:
    default:
        return CreatureAtlasTile::CrewSkinDark;
    }
}

auto moustache_tile(std::uint32_t seed) noexcept -> CreatureAtlasTile {
    return ((seed >> 12U) & 1U) != 0U
               ? CreatureAtlasTile::CrewHairBrown
               : CreatureAtlasTile::CrewHairBlack;
}

auto musket_tile(MusketVisualMaterial material) noexcept
    -> CreatureAtlasTile {
    switch (material) {
    case MusketVisualMaterial::Walnut:
        return CreatureAtlasTile::CrewWood;
    case MusketVisualMaterial::Brass:
        return CreatureAtlasTile::CrewGold;
    case MusketVisualMaterial::DarkBore:
        return CreatureAtlasTile::CrewHairBlack;
    case MusketVisualMaterial::Flint:
    case MusketVisualMaterial::PatinatedSteel:
    default:
        return CreatureAtlasTile::CrewIron;
    }
}

auto musket_material(MusketVisualMaterial material) noexcept -> float {
    return material == MusketVisualMaterial::Walnut
               ? kMaterialWood
               : kMaterialMetal;
}

} // namespace

void append_old_guard_parts(std::vector<CreaturePartInstance>& parts,
                            const OldGuardRenderInstance& guard) {
    const auto first_part = parts.size();
    const auto pose = sample_old_guard_pose(guard);
    const auto skin = skin_tile(guard.appearance_seed);
    const auto moustache = moustache_tile(guard.appearance_seed);
    const auto moustache_width =
        0.052F +
        static_cast<float>((guard.appearance_seed >> 18U) & 0x3U) * 0.009F;
    const auto root = glm::translate(
        pose.body_root,
        pose.body_offset_local);
    const auto world_to_root = glm::inverse(root);

    // Je construis une poitrine, une taille et un bassin qui se recouvrent.
    // L'habit conserve ainsi une carrure humaine même pendant la marche et ne
    // ressemble plus à une succession de blocs séparés.
    append_box(
        parts,
        first_part,
        root,
        glm::vec3 {0.006F, 1.33F, 0.0F},
        glm::vec3 {0.170F, 0.225F, 0.202F},
        glm::vec3 {0.0F},
        CreatureAtlasTile::CrewNavyCloth,
        kMaterialFabric,
        0.13F);
    append_box(
        parts,
        first_part,
        root,
        glm::vec3 {-0.008F, 1.075F, 0.0F},
        glm::vec3 {0.154F, 0.150F, 0.180F},
        glm::vec3 {0.0F},
        CreatureAtlasTile::CrewNavyCloth,
        kMaterialFabric);
    append_box(
        parts,
        first_part,
        root,
        glm::vec3 {-0.020F, 0.875F, 0.0F},
        glm::vec3 {0.150F, 0.135F, 0.170F},
        glm::vec3 {0.0F},
        CreatureAtlasTile::CrewNavyCloth,
        kMaterialFabric);

    // Je pose deux revers étroits et un plastron ivoire sur l'avant du corps :
    // les baudriers restent historiques sans former un énorme X flottant.
    append_box(
        parts,
        first_part,
        root,
        glm::vec3 {0.188F, 1.33F, -0.055F},
        glm::vec3 {0.015F, 0.205F, 0.027F},
        glm::vec3 {0.20F, 0.0F, 0.0F},
        CreatureAtlasTile::CrewIvoryCloth,
        kMaterialFabric);
    append_box(
        parts,
        first_part,
        root,
        glm::vec3 {0.188F, 1.33F, 0.055F},
        glm::vec3 {0.015F, 0.205F, 0.027F},
        glm::vec3 {-0.20F, 0.0F, 0.0F},
        CreatureAtlasTile::CrewIvoryCloth,
        kMaterialFabric);
    append_box(
        parts,
        first_part,
        root,
        glm::vec3 {0.174F, 1.105F, 0.0F},
        glm::vec3 {0.016F, 0.150F, 0.102F},
        glm::vec3 {0.0F},
        CreatureAtlasTile::CrewIvoryCloth,
        kMaterialFabric,
        0.06F);
    append_box(
        parts,
        first_part,
        root,
        glm::vec3 {0.183F, 1.235F, 0.0F},
        glm::vec3 {0.012F, 0.305F, 0.023F},
        glm::vec3 {0.72F, 0.0F, 0.0F},
        CreatureAtlasTile::CrewIvoryCloth,
        kMaterialFabric,
        0.04F);
    append_box(
        parts,
        first_part,
        root,
        glm::vec3 {0.186F, 1.23F, 0.0F},
        glm::vec3 {0.012F, 0.305F, 0.023F},
        glm::vec3 {-0.72F, 0.0F, 0.0F},
        CreatureAtlasTile::CrewIvoryCloth,
        kMaterialFabric,
        0.04F);
    append_box(
        parts,
        first_part,
        root,
        glm::vec3 {0.174F, 0.965F, 0.0F},
        glm::vec3 {0.018F, 0.024F, 0.176F},
        glm::vec3 {0.0F},
        CreatureAtlasTile::CrewLeather,
        kMaterialLeather,
        0.05F);

    for (const auto side : {-1.0F, 1.0F}) {
        append_box(
            parts,
            first_part,
            root,
            glm::vec3 {-0.105F, 0.890F, side * 0.080F},
            glm::vec3 {0.074F, 0.215F, 0.068F},
            glm::vec3 {0.0F, 0.0F, -0.06F},
            CreatureAtlasTile::CrewNavyCloth,
            kMaterialFabric,
            0.12F);
    }

    for (int row = 0; row < 3; ++row) {
        for (const auto side : {-1.0F, 1.0F}) {
            append_box(
                parts,
                first_part,
                root,
                glm::vec3 {
                    0.194F,
                    1.13F + static_cast<float>(row) * 0.14F,
                    side * 0.067F,
                },
                glm::vec3 {0.012F, 0.017F, 0.017F},
                glm::vec3 {0.0F},
                CreatureAtlasTile::CrewGold,
                kMaterialMetal,
                0.03F);
        }
    }

    // Je garde les guetres blanches hautes et les souliers sombres propres a
    // la silhouette de la Vieille Garde, y compris pendant la marche.
    const auto limb_scale = std::clamp(
        pose.stature_scale,
        0.90F,
        1.10F);
    const auto body_forward = safe_direction(
        glm::vec3 {root[0]},
        glm::vec3 {1.0F, 0.0F, 0.0F});
    const auto body_up = safe_direction(
        glm::vec3 {root[1]},
        glm::vec3 {0.0F, 1.0F, 0.0F});
    for (std::size_t index = 0; index < pose.hips.size(); ++index) {
        append_limb_segment(
            parts,
            first_part,
            pose.hips[index],
            pose.knees[index],
            glm::vec2 {0.071F, 0.066F} * limb_scale,
            CreatureAtlasTile::CrewIvoryCloth,
            kMaterialFabric,
            0.10F);
        append_limb_segment(
            parts,
            first_part,
            pose.knees[index],
            pose.ankles[index],
            glm::vec2 {0.063F, 0.059F} * limb_scale,
            CreatureAtlasTile::CrewIvoryCloth,
            kMaterialFabric,
            0.09F);
        append_limb_segment(
            parts,
            first_part,
            pose.ankles[index] - body_forward * (0.03F * limb_scale),
            pose.feet[index] + body_forward * (0.09F * limb_scale),
            glm::vec2 {0.062F, 0.067F} * limb_scale,
            CreatureAtlasTile::CrewHairBlack,
            kMaterialLeather,
            0.08F);
        append_body_space_box(
            parts,
            first_part,
            root,
            world_to_root,
            pose.hips[index],
            {0.076F, 0.080F, 0.073F},
            CreatureAtlasTile::CrewIvoryCloth,
            kMaterialFabric,
            0.10F);
        append_body_space_box(
            parts,
            first_part,
            root,
            world_to_root,
            pose.knees[index],
            {0.066F, 0.064F, 0.062F},
            CreatureAtlasTile::CrewIvoryCloth,
            kMaterialFabric,
            0.09F);
        append_body_space_box(
            parts,
            first_part,
            root,
            world_to_root,
            pose.ankles[index],
            {0.059F, 0.061F, 0.058F},
            CreatureAtlasTile::CrewIvoryCloth,
            kMaterialFabric,
            0.08F);
    }

    for (std::size_t index = 0; index < pose.shoulders.size(); ++index) {
        append_limb_segment(
            parts,
            first_part,
            pose.shoulders[index],
            pose.elbows[index],
            glm::vec2 {0.064F, 0.061F} * limb_scale,
            CreatureAtlasTile::CrewNavyCloth,
            kMaterialFabric,
            0.11F);
        append_limb_segment(
            parts,
            first_part,
            pose.elbows[index],
            pose.hands[index],
            glm::vec2 {0.054F, 0.052F} * limb_scale,
            CreatureAtlasTile::CrewNavyCloth,
            kMaterialFabric,
            0.09F);
        append_segment(
            parts,
            first_part,
            glm::mix(pose.elbows[index], pose.hands[index], 0.62F),
            glm::mix(pose.elbows[index], pose.hands[index], 0.82F),
            glm::vec2 {0.060F, 0.058F} * limb_scale,
            CreatureAtlasTile::CrewRedCloth,
            kMaterialFabric,
            0.08F);
        append_body_space_box(
            parts,
            first_part,
            root,
            world_to_root,
            pose.hands[index],
            glm::vec3 {0.054F, 0.057F, 0.053F},
            skin,
            kMaterialSkin,
            0.07F);
        append_body_space_box(
            parts,
            first_part,
            root,
            world_to_root,
            pose.shoulders[index],
            glm::vec3 {0.078F, 0.079F, 0.075F},
            CreatureAtlasTile::CrewNavyCloth,
            kMaterialFabric,
            0.11F);
        append_body_space_box(
            parts,
            first_part,
            root,
            world_to_root,
            pose.elbows[index],
            glm::vec3 {0.059F, 0.060F, 0.057F},
            CreatureAtlasTile::CrewNavyCloth,
            kMaterialFabric,
            0.09F);
        append_body_space_box(
            parts,
            first_part,
            root,
            world_to_root,
            pose.shoulders[index] +
                body_up * (0.025F * limb_scale),
            glm::vec3 {0.092F, 0.035F, 0.082F},
            CreatureAtlasTile::CrewRedCloth,
            kMaterialFabric,
            0.10F);
    }

    append_box(
        parts,
        first_part,
        root,
        glm::vec3 {0.02F, 1.535F, 0.0F},
        glm::vec3 {0.069F, 0.080F, 0.069F},
        glm::vec3 {0.0F},
        skin,
        kMaterialSkin);
    append_box(
        parts,
        first_part,
        root,
        glm::vec3 {0.085F, 1.505F, 0.0F},
        glm::vec3 {0.090F, 0.035F, 0.105F},
        glm::vec3 {0.0F},
        CreatureAtlasTile::CrewIvoryCloth,
        kMaterialFabric,
        0.08F);
    append_box(
        parts,
        first_part,
        root,
        glm::vec3 {0.03F, 1.70F, 0.0F},
        glm::vec3 {0.145F, 0.155F, 0.140F},
        glm::vec3 {0.0F},
        skin,
        kMaterialSkin,
        0.10F);
    append_box(
        parts,
        first_part,
        root,
        glm::vec3 {0.068F, 1.575F, 0.0F},
        glm::vec3 {0.105F, 0.072F, 0.114F},
        glm::vec3 {0.0F},
        skin,
        kMaterialSkin,
        0.09F);
    append_box(
        parts,
        first_part,
        root,
        glm::vec3 {0.181F, 1.705F, 0.0F},
        glm::vec3 {0.038F, 0.045F, 0.033F},
        glm::vec3 {0.0F},
        skin,
        kMaterialSkin,
        0.05F);
    for (const auto side : {-1.0F, 1.0F}) {
        append_box(
            parts,
            first_part,
            root,
            glm::vec3 {0.172F, 1.755F, side * 0.066F},
            glm::vec3 {0.015F, 0.022F, 0.018F},
            glm::vec3 {0.0F},
            CreatureAtlasTile::VillagerEye,
            kMaterialSkin,
            0.80F);
        append_box(
            parts,
            first_part,
            root,
            glm::vec3 {0.174F, 1.796F, side * 0.066F},
            glm::vec3 {0.012F, 0.010F, 0.042F},
            glm::vec3 {side * 0.05F, 0.0F, 0.0F},
            moustache,
            kMaterialLeather,
            0.10F);
        append_box(
            parts,
            first_part,
            root,
            glm::vec3 {0.010F, 1.700F, side * 0.150F},
            glm::vec3 {0.025F, 0.044F, 0.022F},
            glm::vec3 {0.0F},
            skin,
            kMaterialSkin,
            0.07F);
        append_box(
            parts,
            first_part,
            root,
            glm::vec3 {0.190F, 1.650F, side * moustache_width},
            glm::vec3 {0.017F, 0.020F, moustache_width},
            glm::vec3 {side * 0.18F, 0.0F, 0.0F},
            moustache,
            kMaterialLeather,
            0.12F);
    }
    append_box(
        parts,
        first_part,
        root,
        glm::vec3 {0.178F, 1.615F, 0.0F},
        glm::vec3 {0.010F, 0.010F, 0.042F},
        glm::vec3 {0.0F},
        CreatureAtlasTile::CrewBurgundyCloth,
        kMaterialFabric,
        0.04F);

    // Je monte un bonnet d'ours massif, une plaque frontale et un plumet rouge
    // en plusieurs volumes afin que le profil reste reconnaissable sous tous
    // les angles sans recourir a un modele importe.
    append_box(
        parts,
        first_part,
        root,
        glm::vec3 {-0.01F, 1.965F, 0.0F},
        glm::vec3 {0.158F, 0.200F, 0.154F},
        glm::vec3 {0.0F, 0.0F, -0.035F},
        CreatureAtlasTile::CrewHairBlack,
        kMaterialLeather,
        0.22F);
    append_box(
        parts,
        first_part,
        root,
        glm::vec3 {-0.035F, 2.155F, 0.0F},
        glm::vec3 {0.140F, 0.038F, 0.140F},
        glm::vec3 {0.0F},
        CreatureAtlasTile::CrewHairBlack,
        kMaterialLeather,
        0.18F);
    append_box(
        parts,
        first_part,
        root,
        glm::vec3 {0.152F, 1.985F, 0.0F},
        glm::vec3 {0.018F, 0.087F, 0.080F},
        glm::vec3 {0.0F},
        CreatureAtlasTile::CrewGold,
        kMaterialMetal,
        0.05F);
    for (int section = 0; section < 3; ++section) {
        const auto section_f = static_cast<float>(section);
        append_box(
            parts,
            first_part,
            root,
            glm::vec3 {
                0.0F + section_f * 0.015F,
                2.245F + section_f * 0.090F,
                -0.095F,
            },
            glm::vec3 {
                0.042F - section_f * 0.004F,
                0.064F,
                0.042F - section_f * 0.004F,
            },
            glm::vec3 {0.0F, 0.0F, -0.15F},
            CreatureAtlasTile::CrewRedCloth,
            kMaterialFabric,
            0.10F);
    }

    const auto& musket = pose.musket_transform;

    // Je parcours la recette partagee pour que la crosse profilee, les trois
    // bandes, la platine a silex et les organes de visee gardent les memes
    // proportions que l'arme du joueur et son icone.
    for (const auto& recipe_part : musket_visual_parts()) {
        auto center = recipe_part.center;
        if (recipe_part.kind == MusketVisualPartKind::Ramrod) {
            center.x += pose.ramrod_offset;
        }
        append_box(
            parts,
            first_part,
            musket,
            center,
            recipe_part.half_extent,
            recipe_part.rotation_radians,
            musket_tile(recipe_part.material),
            musket_material(recipe_part.material),
            recipe_part.kind == MusketVisualPartKind::Stock ? 0.13F : 0.05F);
    }

    // Je garde une baionnette exclusivement sur la Vieille Garde. La douille
    // et le coude sous le canon rendent sa fixation credible avant la lame.
    append_box(
        parts,
        first_part,
        musket,
        glm::vec3 {1.105F, 0.038F, 0.0F},
        glm::vec3 {0.055F, 0.034F, 0.034F},
        glm::vec3 {0.0F},
        CreatureAtlasTile::CrewIron,
        kMaterialMetal,
        0.03F);
    append_box(
        parts,
        first_part,
        musket,
        glm::vec3 {1.155F, 0.070F, 0.0F},
        glm::vec3 {0.045F, 0.012F, 0.018F},
        glm::vec3 {0.0F, 0.0F, 0.32F},
        CreatureAtlasTile::CrewIron,
        kMaterialMetal,
        0.03F);
    append_segment(
        parts,
        first_part,
        pose.bayonet_base,
        pose.bayonet_tip,
        glm::vec2 {0.018F, 0.010F},
        CreatureAtlasTile::CrewIron,
        kMaterialMetal,
        0.03F);

    // Je propage l'eclairage local apres la construction pour que toutes les
    // pieces du soldat partagent exactement la meme exposition au pont.
    const auto sky = std::clamp(
        std::isfinite(guard.sky_light) ? guard.sky_light : 1.0F,
        0.0F,
        1.0F);
    const auto local = std::clamp(
        std::isfinite(guard.local_light) ? guard.local_light : 0.0F,
        0.0F,
        1.0F);
    const auto precipitation = std::clamp(
        std::isfinite(guard.precipitation_exposure)
            ? guard.precipitation_exposure
            : 1.0F,
        0.0F,
        1.0F);
    for (auto index = first_part; index < parts.size(); ++index) {
        parts[index].sky_light = sky;
        parts[index].block_light = local;
        parts[index].precipitation_exposure = precipitation;
    }
}

auto build_old_guard_parts(const OldGuardRenderInstance& guard)
    -> std::vector<CreaturePartInstance> {
    std::vector<CreaturePartInstance> parts {};
    parts.reserve(kOldGuardVisualPartBudget);
    append_old_guard_parts(parts, guard);
    return parts;
}

auto build_old_guard_mesh(const OldGuardRenderInstance& guard)
    -> CreatureMeshData {
    const auto parts = build_old_guard_parts(guard);
    return build_creature_mesh(parts);
}

} // namespace valcraft
