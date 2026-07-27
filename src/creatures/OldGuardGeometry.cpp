#include "creatures/OldGuardGeometry.h"

#include "creatures/OldGuardAnimation.h"
#include "render/MusketVisualRecipe.h"

#include <glm/common.hpp>
#include <glm/geometric.hpp>
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
    const auto& root = pose.body_root;

    // Je construis l'habit commun avant les details du visage : le bleu,
    // le blanc et le rouge restent ainsi immediatement lisibles de loin.
    append_box(
        parts,
        first_part,
        root,
        glm::vec3 {0.0F, 1.24F, 0.0F},
        glm::vec3 {0.175F, 0.315F, 0.125F},
        glm::vec3 {0.0F},
        CreatureAtlasTile::CrewNavyCloth,
        kMaterialFabric,
        0.13F);
    append_box(
        parts,
        first_part,
        root,
        glm::vec3 {0.166F, 1.27F, -0.070F},
        glm::vec3 {0.020F, 0.270F, 0.055F},
        glm::vec3 {0.42F, 0.0F, 0.0F},
        CreatureAtlasTile::CrewIvoryCloth,
        kMaterialFabric);
    append_box(
        parts,
        first_part,
        root,
        glm::vec3 {0.166F, 1.27F, 0.070F},
        glm::vec3 {0.020F, 0.270F, 0.055F},
        glm::vec3 {-0.42F, 0.0F, 0.0F},
        CreatureAtlasTile::CrewIvoryCloth,
        kMaterialFabric);
    append_box(
        parts,
        first_part,
        root,
        glm::vec3 {0.0F, 0.93F, 0.0F},
        glm::vec3 {0.170F, 0.115F, 0.125F},
        glm::vec3 {0.0F},
        CreatureAtlasTile::CrewIvoryCloth,
        kMaterialFabric);
    append_box(
        parts,
        first_part,
        root,
        glm::vec3 {0.175F, 1.21F, 0.0F},
        glm::vec3 {0.018F, 0.020F, 0.180F},
        glm::vec3 {0.0F},
        CreatureAtlasTile::CrewLeather,
        kMaterialLeather,
        0.05F);
    append_box(
        parts,
        first_part,
        root,
        glm::vec3 {0.183F, 1.23F, 0.0F},
        glm::vec3 {0.014F, 0.350F, 0.035F},
        glm::vec3 {0.72F, 0.0F, 0.0F},
        CreatureAtlasTile::CrewIvoryCloth,
        kMaterialFabric,
        0.04F);
    append_box(
        parts,
        first_part,
        root,
        glm::vec3 {0.186F, 1.23F, 0.0F},
        glm::vec3 {0.014F, 0.350F, 0.035F},
        glm::vec3 {-0.72F, 0.0F, 0.0F},
        CreatureAtlasTile::CrewIvoryCloth,
        kMaterialFabric,
        0.04F);

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
    for (std::size_t index = 0; index < pose.hips.size(); ++index) {
        append_segment(
            parts,
            first_part,
            pose.hips[index],
            pose.knees[index],
            glm::vec2 {0.065F, 0.060F},
            CreatureAtlasTile::CrewIvoryCloth,
            kMaterialFabric,
            0.10F);
        append_segment(
            parts,
            first_part,
            pose.knees[index],
            pose.ankles[index],
            glm::vec2 {0.058F, 0.054F},
            CreatureAtlasTile::CrewIvoryCloth,
            kMaterialFabric,
            0.09F);
        append_segment(
            parts,
            first_part,
            pose.ankles[index] - glm::vec3 {0.03F, 0.0F, 0.0F},
            pose.feet[index] + glm::vec3 {0.09F, 0.0F, 0.0F},
            glm::vec2 {0.055F, 0.060F},
            CreatureAtlasTile::CrewLeather,
            kMaterialLeather,
            0.08F);
    }

    for (std::size_t index = 0; index < pose.shoulders.size(); ++index) {
        append_segment(
            parts,
            first_part,
            pose.shoulders[index],
            pose.elbows[index],
            glm::vec2 {0.058F, 0.055F},
            CreatureAtlasTile::CrewNavyCloth,
            kMaterialFabric,
            0.11F);
        append_segment(
            parts,
            first_part,
            pose.elbows[index],
            pose.hands[index],
            glm::vec2 {0.049F, 0.047F},
            CreatureAtlasTile::CrewNavyCloth,
            kMaterialFabric,
            0.09F);
        append_segment(
            parts,
            first_part,
            glm::mix(pose.elbows[index], pose.hands[index], 0.62F),
            glm::mix(pose.elbows[index], pose.hands[index], 0.82F),
            glm::vec2 {0.057F, 0.055F},
            CreatureAtlasTile::CrewRedCloth,
            kMaterialFabric,
            0.08F);
        append_box(
            parts,
            first_part,
            glm::mat4 {1.0F},
            pose.hands[index],
            glm::vec3 {0.050F, 0.052F, 0.050F},
            glm::vec3 {0.0F},
            skin,
            kMaterialSkin,
            0.07F);
        append_box(
            parts,
            first_part,
            glm::mat4 {1.0F},
            pose.shoulders[index] + glm::vec3 {0.0F, 0.025F, 0.0F},
            glm::vec3 {0.095F, 0.032F, 0.085F},
            glm::vec3 {0.0F},
            CreatureAtlasTile::CrewRedCloth,
            kMaterialFabric,
            0.10F);
    }

    append_box(
        parts,
        first_part,
        root,
        glm::vec3 {0.02F, 1.54F, 0.0F},
        glm::vec3 {0.063F, 0.060F, 0.063F},
        glm::vec3 {0.0F},
        CreatureAtlasTile::CrewHairBlack,
        kMaterialLeather);
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
        glm::vec3 {0.186F, 1.705F, 0.0F},
        glm::vec3 {0.045F, 0.050F, 0.044F},
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
            glm::vec3 {0.190F, 1.650F, side * moustache_width},
            glm::vec3 {0.017F, 0.020F, moustache_width},
            glm::vec3 {side * 0.18F, 0.0F, 0.0F},
            moustache,
            kMaterialLeather,
            0.12F);
    }

    // Je monte un bonnet d'ours massif, une plaque frontale et un plumet rouge
    // en plusieurs volumes afin que le profil reste reconnaissable sous tous
    // les angles sans recourir a un modele importe.
    append_box(
        parts,
        first_part,
        root,
        glm::vec3 {-0.01F, 1.99F, 0.0F},
        glm::vec3 {0.165F, 0.235F, 0.160F},
        glm::vec3 {0.0F, 0.0F, -0.035F},
        CreatureAtlasTile::CrewHairBlack,
        kMaterialLeather,
        0.22F);
    append_box(
        parts,
        first_part,
        root,
        glm::vec3 {-0.035F, 2.215F, 0.0F},
        glm::vec3 {0.145F, 0.045F, 0.145F},
        glm::vec3 {0.0F},
        CreatureAtlasTile::CrewHairBlack,
        kMaterialLeather,
        0.18F);
    append_box(
        parts,
        first_part,
        root,
        glm::vec3 {0.158F, 2.015F, 0.0F},
        glm::vec3 {0.018F, 0.100F, 0.087F},
        glm::vec3 {0.0F},
        CreatureAtlasTile::CrewGold,
        kMaterialMetal,
        0.05F);
    for (int section = 0; section < 4; ++section) {
        const auto section_f = static_cast<float>(section);
        append_box(
            parts,
            first_part,
            root,
            glm::vec3 {
                0.0F + section_f * 0.018F,
                2.31F + section_f * 0.105F,
                -0.105F,
            },
            glm::vec3 {
                0.045F - section_f * 0.004F,
                0.075F,
                0.045F - section_f * 0.004F,
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
