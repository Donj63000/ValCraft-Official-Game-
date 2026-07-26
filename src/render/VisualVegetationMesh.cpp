#include "render/VisualVegetationMesh.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <stdexcept>
#include <vector>

namespace valcraft {

namespace {

inline constexpr std::uint16_t kVisualSurfaceFlagCutout = 1U;
inline constexpr float kLightingNormalProbeDistance = 0.42F;
inline constexpr float kLightingUpwardProbeDistance = 0.34F;
inline constexpr float kTau = 6.28318530717958647692F;

[[nodiscard]] auto requires_alpha_cutout(
    const VisualVegetationInstance& instance) noexcept -> bool {
    switch (instance.primitive) {
    case VisualVegetationPrimitive::GrassBlade:
    case VisualVegetationPrimitive::FlowerPetals:
    case VisualVegetationPrimitive::Impostor:
    case VisualVegetationPrimitive::LeafSpray:
        return true;
    case VisualVegetationPrimitive::SimplifiedBouquet:
        // Je garde les bouquets d'herbes et de fleurs ajourés, mais je rends
        // les canopées fermées et les cactus opaques. Une alpha de feuillage
        // répétée en triplanaire sur un ellipsoïde produirait sinon un
        // criblage sombre instable au lieu d'une silhouette végétale pleine.
        return instance.source_kind !=
                   VisualVegetationSourceKind::BroadleafTree &&
               instance.source_kind !=
                   VisualVegetationSourceKind::PineTree &&
               instance.source_kind !=
                   VisualVegetationSourceKind::Cactus;
    case VisualVegetationPrimitive::TaperedTrunk:
    case VisualVegetationPrimitive::EllipsoidCanopy:
    case VisualVegetationPrimitive::ConicalCanopy:
    case VisualVegetationPrimitive::CactusStem:
    case VisualVegetationPrimitive::CactusArm:
    case VisualVegetationPrimitive::FlowerStem:
        return false;
    }
    return false;
}

[[nodiscard]] auto primitive_type_for(
    const VisualVegetationInstance& instance) noexcept
    -> StylizedPrimitiveType {
    switch (instance.primitive) {
    case VisualVegetationPrimitive::TaperedTrunk:
    case VisualVegetationPrimitive::CactusStem:
    case VisualVegetationPrimitive::CactusArm:
    case VisualVegetationPrimitive::FlowerStem:
        return StylizedPrimitiveType::TaperedCylinder;
    case VisualVegetationPrimitive::EllipsoidCanopy:
    case VisualVegetationPrimitive::ConicalCanopy:
        return StylizedPrimitiveType::Ellipsoid;
    case VisualVegetationPrimitive::GrassBlade:
        return StylizedPrimitiveType::Ribbon;
    case VisualVegetationPrimitive::FlowerPetals:
    case VisualVegetationPrimitive::Impostor:
    case VisualVegetationPrimitive::LeafSpray:
        return StylizedPrimitiveType::Panel;
    case VisualVegetationPrimitive::SimplifiedBouquet:
        if (instance.source_kind == VisualVegetationSourceKind::BroadleafTree ||
            instance.source_kind == VisualVegetationSourceKind::PineTree) {
            return StylizedPrimitiveType::Ellipsoid;
        }
        if (instance.source_kind == VisualVegetationSourceKind::Cactus) {
            return StylizedPrimitiveType::TaperedCylinder;
        }
        return StylizedPrimitiveType::Ribbon;
    default:
        return StylizedPrimitiveType::Panel;
    }
}

[[nodiscard]] auto is_tree_source(
    const VisualVegetationInstance& instance) noexcept -> bool {
    return instance.source_kind ==
               VisualVegetationSourceKind::BroadleafTree ||
           instance.source_kind ==
               VisualVegetationSourceKind::PineTree;
}

[[nodiscard]] auto is_tree_canopy(
    const VisualVegetationInstance& instance) noexcept -> bool {
    if (!is_tree_source(instance)) {
        return false;
    }
    switch (instance.primitive) {
    case VisualVegetationPrimitive::EllipsoidCanopy:
    case VisualVegetationPrimitive::ConicalCanopy:
    case VisualVegetationPrimitive::SimplifiedBouquet:
        return true;
    case VisualVegetationPrimitive::TaperedTrunk:
    case VisualVegetationPrimitive::CactusStem:
    case VisualVegetationPrimitive::CactusArm:
    case VisualVegetationPrimitive::GrassBlade:
    case VisualVegetationPrimitive::FlowerStem:
    case VisualVegetationPrimitive::FlowerPetals:
    case VisualVegetationPrimitive::Impostor:
    case VisualVegetationPrimitive::LeafSpray:
        return false;
    }
    return false;
}

[[nodiscard]] auto effective_primitive_lod(
    const VisualVegetationInstance& instance,
    VisualVegetationLod vegetation_lod,
    StylizedPrimitiveLod requested_lod) noexcept
    -> StylizedPrimitiveLod {
    if (requested_lod != StylizedPrimitiveLod::Low ||
        vegetation_lod == VisualVegetationLod::Far) {
        return requested_lod;
    }
    // Je garde 16 pans sur le tronc pour éviter un poteau octogonal. En
    // revanche, les trois lobes Medium utilisent chacun l'ellipsoïde Low :
    // leur chevauchement masque les facettes et borne la recette à 304
    // triangles par feuillu. Near conserve la finesse supérieure.
    if (instance.primitive ==
            VisualVegetationPrimitive::TaperedTrunk &&
        is_tree_source(instance)) {
        return StylizedPrimitiveLod::Medium;
    }
    if (vegetation_lod == VisualVegetationLod::Near &&
        is_tree_canopy(instance)) {
        return StylizedPrimitiveLod::Medium;
    }
    return requested_lod;
}

[[nodiscard]] auto safe_inverse_scale(float scale) noexcept -> float {
    return 1.0F / std::max(std::abs(scale), 1.0e-4F);
}

[[nodiscard]] auto sample_lighting(
    const VisualVegetationLightingSampler& lighting_sampler,
    float world_x,
    float world_y,
    float world_z,
    float normal_x,
    float normal_y,
    float normal_z) -> VisualVegetationLighting {
    const auto sample = [&lighting_sampler](
                            float x,
                            float y,
                            float z) {
        return lighting_sampler(
            static_cast<int>(std::floor(x)),
            static_cast<int>(std::floor(y)),
            static_cast<int>(std::floor(z)));
    };
    const auto surface = sample(world_x, world_y, world_z);
    const auto outward = sample(
        world_x + normal_x * kLightingNormalProbeDistance,
        world_y + normal_y * kLightingNormalProbeDistance,
        world_z + normal_z * kLightingNormalProbeDistance);
    const auto upward = sample(
        world_x,
        world_y + kLightingUpwardProbeDistance,
        world_z);

    // Je ne réutilise plus la lumière de la cellule source opaque. Je sonde
    // la surface réellement dessinée et son extérieur, puis je conserve la
    // meilleure contribution logique. Les troncs et canopées restent ainsi
    // lisibles sans inventer une lumière ni modifier sa simulation.
    return VisualVegetationLighting {
        std::min<std::uint8_t>(
            std::max({surface.sky_light,
                      outward.sky_light,
                      upward.sky_light}),
            15U),
        std::min<std::uint8_t>(
            std::max({surface.block_light,
                      outward.block_light,
                      upward.block_light}),
            15U),
    };
}

void append_instance(
    OrganicTerrainMesh& destination,
    const VisualVegetationInstance& instance,
    StylizedPrimitiveLod primitive_lod,
    const VisualVegetationLightingSampler& lighting_sampler) {
    const auto primitive = build_stylized_primitive(
        primitive_type_for(instance),
        primitive_lod);
    if (primitive.empty()) {
        return;
    }
    if (destination.vertices.size() >
        static_cast<std::size_t>(std::numeric_limits<std::uint32_t>::max()) -
            primitive.vertices.size()) {
        throw std::length_error(
            "Le maillage de végétation dépasse la plage de ses indices");
    }

    const auto base_index =
        static_cast<std::uint32_t>(destination.vertices.size());
    const auto cosine = std::cos(instance.yaw_radians);
    const auto sine = std::sin(instance.yaw_radians);
    const auto surface_flags = requires_alpha_cutout(instance)
        ? kVisualSurfaceFlagCutout
        : std::uint16_t {0U};

    destination.vertices.reserve(
        destination.vertices.size() + primitive.vertices.size());
    for (const auto& source : primitive.vertices) {
        auto local_x = source.x;
        auto local_y = source.y;
        auto local_z = source.z;
        if (is_tree_canopy(instance)) {
            const auto phase =
                static_cast<float>(instance.seed & 0xFFFFU) /
                65535.0F * kTau;
            const auto contour_x = std::sin(
                phase +
                source.x * 5.1F +
                source.y * 2.7F +
                source.z * 7.3F);
            const auto contour_z = std::cos(
                phase * 0.73F -
                source.x * 6.4F +
                source.y * 3.9F +
                source.z * 4.6F);
            const auto contour_y = std::sin(
                phase * 1.31F +
                source.x * 3.2F -
                source.y * 5.7F +
                source.z * 2.4F);
            // Je déforme uniquement vers l'intérieur du gabarit canonique :
            // chaque lobe perd sa sphéricité sans jamais sortir de ses bounds.
            local_x *= 0.93F + (contour_x * 0.5F + 0.5F) * 0.06F;
            local_y *= 0.95F + (contour_y * 0.5F + 0.5F) * 0.035F;
            local_z *= 0.92F + (contour_z * 0.5F + 0.5F) * 0.07F;
            const auto margin_x =
                std::max(0.0F, 0.5F - std::abs(local_x));
            const auto margin_z =
                std::max(0.0F, 0.5F - std::abs(local_z));
            local_x +=
                contour_z * source.y * margin_x * 0.38F;
            local_z +=
                contour_x * source.y * margin_z * 0.34F;
        }
        if (instance.primitive ==
                VisualVegetationPrimitive::TaperedTrunk &&
            is_tree_source(instance)) {
            const auto normalized_height =
                std::clamp(source.y + 0.5F, 0.0F, 1.0F);
            const auto bend =
                normalized_height * normalized_height * 0.072F;
            const auto bend_phase =
                static_cast<float>(instance.seed & 0xFFFFU) /
                65535.0F * kTau;
            // Le rayon supérieur laisse une marge interne. J'y inscris une
            // courbure déterministe sans sortir des bornes ni déplacer la base.
            local_x += std::cos(bend_phase) * bend;
            local_z += std::sin(bend_phase) * bend;
        }

        const auto scaled_x = local_x * instance.scale_x;
        const auto scaled_z = local_z * instance.scale_z;
        const auto world_x =
            instance.position_x + cosine * scaled_x + sine * scaled_z;
        const auto world_y =
            instance.position_y + local_y * instance.scale_y;
        const auto world_z =
            instance.position_z - sine * scaled_x + cosine * scaled_z;

        const auto inverse_x = source.nx * safe_inverse_scale(instance.scale_x);
        const auto inverse_y = source.ny * safe_inverse_scale(instance.scale_y);
        const auto inverse_z = source.nz * safe_inverse_scale(instance.scale_z);
        auto normal_x = cosine * inverse_x + sine * inverse_z;
        auto normal_y = inverse_y;
        auto normal_z = -sine * inverse_x + cosine * inverse_z;
        const auto normal_length = std::sqrt(
            normal_x * normal_x +
            normal_y * normal_y +
            normal_z * normal_z);
        const auto inverse_length =
            1.0F / std::max(normal_length, 1.0e-6F);
        normal_x *= inverse_length;
        normal_y *= inverse_length;
        normal_z *= inverse_length;

        const auto lighting = sample_lighting(
            lighting_sampler,
            world_x,
            world_y,
            world_z,
            normal_x,
            normal_y,
            normal_z);
        destination.vertices.push_back({
            world_x,
            world_y,
            world_z,
            normal_x,
            normal_y,
            normal_z,
            instance.material_block,
            to_block_id(BlockType::Air),
            0U,
            255U,
            static_cast<std::uint8_t>(
                std::min<std::uint8_t>(lighting.sky_light, 15U)),
            static_cast<std::uint8_t>(
                std::min<std::uint8_t>(lighting.block_light, 15U)),
            surface_flags,
        });
    }

    destination.indices.reserve(
        destination.indices.size() + primitive.indices.size());
    for (const auto index : primitive.indices) {
        destination.indices.push_back(base_index + index);
    }
}

} // namespace

auto build_visual_vegetation_mesh(
    const VisualVegetationBuild& build,
    VisualVegetationLod lod,
    StylizedPrimitiveLod primitive_lod,
    const VisualVegetationLightingSampler& lighting_sampler)
    -> OrganicTerrainMesh {
    OrganicTerrainMesh result {};
    if (!lighting_sampler) {
        return result;
    }

    const auto lod_index = visual_vegetation_lod_index(lod);
    if (lod_index >= build.lods.size()) {
        return result;
    }
    const auto& batch = build.lods[lod_index];
    for (const auto& instance : batch.instances) {
        append_instance(
            result,
            instance,
            effective_primitive_lod(
                instance,
                lod,
                primitive_lod),
            lighting_sampler);
    }
    result.quad_count = result.indices.size() / 6U;
    return result;
}

auto partition_visual_vegetation_mesh(
    const OrganicTerrainMesh& mesh,
    int minimum_y,
    int section_height,
    std::size_t section_count) -> std::vector<OrganicTerrainMesh> {
    if (section_height <= 0 || section_count == 0U) {
        throw std::invalid_argument(
            "Le partitionnement de vegetation exige des sections valides");
    }
    if (mesh.indices.size() % 3U != 0U) {
        throw std::invalid_argument(
            "Le maillage de vegetation doit contenir des triangles complets");
    }

    std::vector<OrganicTerrainMesh> sections(section_count);
    const auto section_for_y =
        [minimum_y, section_height, section_count](double y) noexcept {
            const auto relative_y =
                y - static_cast<double>(minimum_y);
            auto section = static_cast<long long>(
                std::floor(
                    relative_y /
                    static_cast<double>(section_height)));
            section = std::clamp(
                section,
                0LL,
                static_cast<long long>(section_count - 1U));
            return static_cast<std::size_t>(section);
        };

    for (std::size_t index_offset = 0U;
         index_offset < mesh.indices.size();
         index_offset += 3U) {
        const auto index_a = mesh.indices[index_offset];
        const auto index_b = mesh.indices[index_offset + 1U];
        const auto index_c = mesh.indices[index_offset + 2U];
        if (index_a >= mesh.vertices.size() ||
            index_b >= mesh.vertices.size() ||
            index_c >= mesh.vertices.size()) {
            throw std::invalid_argument(
                "Le maillage de vegetation contient un indice invalide");
        }

        const auto& vertex_a = mesh.vertices[index_a];
        const auto& vertex_b = mesh.vertices[index_b];
        const auto& vertex_c = mesh.vertices[index_c];
        const auto centroid_y =
            (static_cast<double>(vertex_a.y) +
             static_cast<double>(vertex_b.y) +
             static_cast<double>(vertex_c.y)) /
            3.0;
        if (!std::isfinite(centroid_y)) {
            throw std::invalid_argument(
                "Le maillage de vegetation contient une hauteur non finie");
        }

        auto& destination =
            sections[section_for_y(centroid_y)];
        if (destination.vertices.size() >
            static_cast<std::size_t>(
                std::numeric_limits<std::uint32_t>::max()) -
                3U) {
            throw std::length_error(
                "Une section de vegetation depasse la plage de ses indices");
        }
        const auto base_index =
            static_cast<std::uint32_t>(
                destination.vertices.size());
        destination.vertices.push_back(vertex_a);
        destination.vertices.push_back(vertex_b);
        destination.vertices.push_back(vertex_c);
        destination.indices.push_back(base_index);
        destination.indices.push_back(base_index + 1U);
        destination.indices.push_back(base_index + 2U);
    }

    for (auto& section : sections) {
        section.quad_count = section.indices.size() / 6U;
    }
    return sections;
}

} // namespace valcraft
