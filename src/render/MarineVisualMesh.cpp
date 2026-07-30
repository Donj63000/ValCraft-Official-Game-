#include "render/MarineVisualMesh.h"

#include "gameplay/SeaAdventure.h"

#include <glm/common.hpp>
#include <glm/geometric.hpp>
#include <glm/vec4.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <numbers>
#include <vector>

namespace valcraft {
namespace {

constexpr float kMinimumScale = 0.001F;
constexpr float kFishHalfLengthScale = 0.92F;
constexpr float kFishHalfHeightScale = 0.48F;

struct DecorCandidate {
    const MarineDecorInstance* instance = nullptr;
    float distance_squared = 0.0F;
    std::uint32_t stable_key = 0U;
};

[[nodiscard]] auto quantized_stable_coordinate(
    float position) noexcept -> std::int32_t {
    const auto quantized =
        std::floor(static_cast<double>(position) * 8.0);
    constexpr auto minimum =
        static_cast<double>(
            (std::numeric_limits<std::int32_t>::min)());
    constexpr auto maximum =
        static_cast<double>(
            (std::numeric_limits<std::int32_t>::max)());
    if (quantized <= minimum) {
        return (std::numeric_limits<std::int32_t>::min)();
    }
    if (quantized >= maximum) {
        return (std::numeric_limits<std::int32_t>::max)();
    }
    return static_cast<std::int32_t>(quantized);
}

[[nodiscard]] auto finite_instance(
    const MarineDecorInstance& instance) noexcept -> bool {
    return std::isfinite(instance.position_x) &&
           std::isfinite(instance.position_y) &&
           std::isfinite(instance.position_z) &&
           std::isfinite(instance.scale_x) &&
           std::isfinite(instance.scale_y) &&
           std::isfinite(instance.scale_z) &&
           std::isfinite(instance.yaw_radians) &&
           std::isfinite(instance.phase) &&
           instance.scale_x > kMinimumScale &&
           instance.scale_y > kMinimumScale &&
           instance.scale_z > kMinimumScale &&
           is_known_visual_material_id(instance.material) &&
           instance.material != VisualMaterialId::None;
}

[[nodiscard]] auto stable_instance_key(
    const MarineDecorInstance& instance) noexcept -> std::uint32_t {
    // Je borne la quantification avant le cast signé : le monde accepte des
    // coordonnées bien plus grandes que la plage int32 après multiplication.
    const auto x =
        quantized_stable_coordinate(instance.position_x);
    const auto z =
        quantized_stable_coordinate(instance.position_z);
    auto value =
        static_cast<std::uint32_t>(x) * 0x9e3779b9U ^
        static_cast<std::uint32_t>(z) * 0x85ebca6bU ^
        static_cast<std::uint32_t>(instance.kind) * 0xc2b2ae35U;
    value ^= value >> 16U;
    value *= 0x7feb352dU;
    value ^= value >> 15U;
    value *= 0x846ca68bU;
    return value ^ (value >> 16U);
}

[[nodiscard]] auto quantized_unit(float value) noexcept -> std::uint8_t {
    return static_cast<std::uint8_t>(
        std::lround(glm::clamp(value, 0.0F, 1.0F) * 255.0F));
}

[[nodiscard]] auto marine_sky_light(float world_y) noexcept -> std::uint8_t {
    const auto depth = std::max(
        static_cast<float>(kSeaLevel + 1) - world_y,
        0.0F);
    const auto light =
        15.0F - glm::clamp(depth * 0.28F, 0.0F, 8.0F);
    return static_cast<std::uint8_t>(std::lround(light));
}

[[nodiscard]] auto make_vertex(
    const glm::vec3& position,
    const glm::vec3& normal,
    VisualMaterialId material,
    float u,
    float v,
    bool cutout,
    std::uint16_t extra_flags = 0U,
    std::uint8_t block_light = 0U) noexcept -> TerrainVertex {
    const auto safe_normal =
        glm::dot(normal, normal) >
                std::numeric_limits<float>::epsilon()
            ? glm::normalize(normal)
            : glm::vec3 {0.0F, 1.0F, 0.0F};
    TerrainVertex vertex {};
    vertex.x = position.x;
    vertex.y = position.y;
    vertex.z = position.z;
    vertex.nx = safe_normal.x;
    vertex.ny = safe_normal.y;
    vertex.nz = safe_normal.z;
    vertex.primary_block_id =
        direct_visual_material_token(material);
    vertex.secondary_block_id =
        cutout ? quantized_unit(u) : 0U;
    vertex.material_blend =
        cutout ? quantized_unit(v) : 0U;
    vertex.ambient_occlusion = 242U;
    vertex.sky_light = marine_sky_light(position.y);
    vertex.block_light = block_light;
    vertex.surface_flags =
        kTerrainSurfaceFlagDirectMaterial |
        extra_flags |
        (cutout ? kTerrainSurfaceFlagCutout : 0U);
    return vertex;
}

void append_quad(
    OrganicTerrainMesh& mesh,
    const std::array<glm::vec3, 4U>& positions,
    const glm::vec3& normal,
    VisualMaterialId material,
    std::uint16_t extra_flags) {
    const auto base =
        static_cast<std::uint32_t>(mesh.vertices.size());
    constexpr std::array<glm::vec2, 4U> uvs {{
        {0.0F, 0.0F},
        {1.0F, 0.0F},
        {1.0F, 1.0F},
        {0.0F, 1.0F},
    }};
    for (std::size_t index = 0U;
         index < positions.size();
         ++index) {
        mesh.vertices.push_back(
            make_vertex(
                positions[index],
                normal,
                material,
                uvs[index].x,
                uvs[index].y,
                true,
                extra_flags));
    }
    mesh.indices.insert(
        mesh.indices.end(),
        {
            base + 0U,
            base + 1U,
            base + 2U,
            base + 0U,
            base + 2U,
            base + 3U,
        });
    ++mesh.quad_count;
}

void append_ribbon(
    OrganicTerrainMesh& mesh,
    const MarineDecorInstance& instance,
    float angle_offset,
    float lateral_offset,
    float width_scale,
    float height_scale,
    int segment_count) {
    const auto angle = instance.yaw_radians + angle_offset;
    const glm::vec3 right {
        std::cos(angle),
        0.0F,
        std::sin(angle),
    };
    const glm::vec3 normal {
        -right.z,
        0.0F,
        right.x,
    };
    const auto origin =
        glm::vec3 {
            instance.position_x,
            instance.position_y,
            instance.position_z,
        } +
        right * (lateral_offset * instance.scale_x);
    const auto half_width =
        std::max(
            instance.scale_x * width_scale * 0.5F,
            0.018F);
    const auto height =
        std::max(
            instance.scale_y * height_scale,
            0.04F);
    const auto segments =
        std::clamp(segment_count, 1, 4);
    const auto base =
        static_cast<std::uint32_t>(mesh.vertices.size());

    for (int segment = 0;
         segment <= segments;
         ++segment) {
        const auto vertical =
            static_cast<float>(segment) /
            static_cast<float>(segments);
        const auto taper =
            glm::mix(1.0F, 0.38F, vertical);
        const auto curve =
            std::sin(
                vertical * std::numbers::pi_v<float> +
                instance.phase) *
            instance.scale_x *
            0.10F *
            vertical;
        const auto center =
            origin +
            glm::vec3 {0.0F, height * vertical, 0.0F} +
            normal * curve;
        mesh.vertices.push_back(
            make_vertex(
                center - right * half_width * taper,
                normal,
                instance.material,
                0.0F,
                vertical,
                true,
                kTerrainSurfaceFlagUnderwaterSway));
        mesh.vertices.push_back(
            make_vertex(
                center + right * half_width * taper,
                normal,
                instance.material,
                1.0F,
                vertical,
                true,
                kTerrainSurfaceFlagUnderwaterSway));
    }

    for (int segment = 0;
         segment < segments;
         ++segment) {
        const auto row =
            base +
            static_cast<std::uint32_t>(segment * 2);
        mesh.indices.insert(
            mesh.indices.end(),
            {
                row + 0U,
                row + 1U,
                row + 3U,
                row + 0U,
                row + 3U,
                row + 2U,
            });
        ++mesh.quad_count;
    }
}

void append_tapered_prism(
    OrganicTerrainMesh& mesh,
    const glm::vec3& start,
    const glm::vec3& end,
    float radius,
    VisualMaterialId material) {
    const auto axis = end - start;
    if (glm::dot(axis, axis) <= 0.000001F ||
        !(radius > kMinimumScale)) {
        return;
    }
    const auto direction = glm::normalize(axis);
    const auto reference =
        std::abs(direction.y) > 0.94F
            ? glm::vec3 {1.0F, 0.0F, 0.0F}
            : glm::vec3 {0.0F, 1.0F, 0.0F};
    const auto tangent = glm::normalize(
        glm::cross(direction, reference));
    const auto bitangent = glm::normalize(
        glm::cross(direction, tangent));
    const auto base =
        static_cast<std::uint32_t>(mesh.vertices.size());

    for (int end_index = 0;
         end_index < 2;
         ++end_index) {
        const auto center =
            end_index == 0 ? start : end;
        const auto ring_radius =
            radius *
            (end_index == 0 ? 1.0F : 0.38F);
        for (int side = 0;
             side < 3;
             ++side) {
            const auto angle =
                static_cast<float>(side) *
                (2.0F * std::numbers::pi_v<float> / 3.0F);
            const auto radial =
                tangent * std::cos(angle) +
                bitangent * std::sin(angle);
            mesh.vertices.push_back(
                make_vertex(
                    center + radial * ring_radius,
                    radial,
                    material,
                    0.0F,
                    0.0F,
                    false));
        }
    }

    for (std::uint32_t side = 0U;
         side < 3U;
         ++side) {
        const auto next = (side + 1U) % 3U;
        mesh.indices.insert(
            mesh.indices.end(),
            {
                base + side,
                base + next,
                base + 3U + next,
                base + side,
                base + 3U + next,
                base + 3U + side,
            });
    }
    mesh.indices.insert(
        mesh.indices.end(),
        {
            base + 3U,
            base + 4U,
            base + 5U,
        });
}

[[nodiscard]] auto rotate_local(
    const MarineDecorInstance& instance,
    const glm::vec3& local) noexcept -> glm::vec3 {
    const auto cosine = std::cos(instance.yaw_radians);
    const auto sine = std::sin(instance.yaw_radians);
    return {
        instance.position_x +
            local.x * cosine -
            local.z * sine,
        instance.position_y + local.y,
        instance.position_z +
            local.x * sine +
            local.z * cosine,
    };
}

void append_branch_coral(
    OrganicTerrainMesh& mesh,
    const MarineDecorInstance& instance,
    bool near_detail) {
    const auto height = instance.scale_y;
    const auto radius =
        std::max(instance.scale_x * 0.105F, 0.025F);
    const auto append_local_branch =
        [&](const glm::vec3& start,
            const glm::vec3& end,
            float radius_scale) {
            append_tapered_prism(
                mesh,
                rotate_local(instance, start),
                rotate_local(instance, end),
                radius * radius_scale,
                instance.material);
        };

    append_local_branch(
        {0.0F, 0.0F, 0.0F},
        {0.0F, height, 0.0F},
        1.0F);
    append_local_branch(
        {0.0F, height * 0.26F, 0.0F},
        {instance.scale_x * 0.38F, height * 0.76F, instance.scale_z * 0.12F},
        0.72F);
    append_local_branch(
        {0.0F, height * 0.34F, 0.0F},
        {-instance.scale_x * 0.34F, height * 0.84F, -instance.scale_z * 0.16F},
        0.68F);
    if (near_detail) {
        append_local_branch(
            {0.0F, height * 0.50F, 0.0F},
            {instance.scale_x * 0.18F, height * 0.96F, -instance.scale_z * 0.34F},
            0.52F);
        append_local_branch(
            {0.0F, height * 0.58F, 0.0F},
            {-instance.scale_x * 0.16F, height * 0.92F, instance.scale_z * 0.31F},
            0.48F);
    }
}

void append_shell(
    OrganicTerrainMesh& mesh,
    const MarineDecorInstance& instance) {
    constexpr std::uint32_t kSegments = 8U;
    const auto base =
        static_cast<std::uint32_t>(mesh.vertices.size());
    const auto center = glm::vec3 {
        instance.position_x,
        instance.position_y + 0.015F,
        instance.position_z,
    };
    mesh.vertices.push_back(
        make_vertex(
            center + glm::vec3 {0.0F, instance.scale_y, 0.0F},
            {0.0F, 1.0F, 0.0F},
            instance.material,
            0.0F,
            0.0F,
            false));
    for (std::uint32_t segment = 0U;
         segment < kSegments;
         ++segment) {
        const auto angle =
            instance.yaw_radians +
            static_cast<float>(segment) *
                (2.0F * std::numbers::pi_v<float> /
                 static_cast<float>(kSegments));
        const glm::vec3 radial {
            std::cos(angle),
            0.35F,
            std::sin(angle),
        };
        mesh.vertices.push_back(
            make_vertex(
                center +
                    glm::vec3 {
                        std::cos(angle) * instance.scale_x,
                        0.0F,
                        std::sin(angle) * instance.scale_z,
                    },
                radial,
                instance.material,
                0.0F,
                0.0F,
                false));
    }
    mesh.vertices.push_back(
        make_vertex(
            center,
            {0.0F, -1.0F, 0.0F},
            instance.material,
            0.0F,
            0.0F,
            false));
    const auto bottom = base + 1U + kSegments;
    for (std::uint32_t segment = 0U;
         segment < kSegments;
         ++segment) {
        const auto next =
            (segment + 1U) % kSegments;
        mesh.indices.insert(
            mesh.indices.end(),
            {
                base,
                base + 1U + segment,
                base + 1U + next,
                bottom,
                base + 1U + next,
                base + 1U + segment,
            });
    }
}

void append_decor_instance(
    OrganicTerrainMesh& mesh,
    const MarineDecorInstance& instance,
    bool near_detail) {
    switch (instance.kind) {
    case MarineDecorKind::Seagrass:
        append_ribbon(
            mesh,
            instance,
            0.0F,
            -0.22F,
            0.34F,
            0.82F,
            near_detail ? 2 : 1);
        append_ribbon(
            mesh,
            instance,
            1.12F,
            0.06F,
            0.30F,
            1.0F,
            near_detail ? 2 : 1);
        if (near_detail) {
            append_ribbon(
                mesh,
                instance,
                2.18F,
                0.24F,
                0.28F,
                0.72F,
                2);
        }
        break;
    case MarineDecorKind::Kelp:
        append_ribbon(
            mesh,
            instance,
            0.0F,
            -0.16F,
            0.92F,
            1.0F,
            near_detail ? 4 : 2);
        append_ribbon(
            mesh,
            instance,
            1.42F,
            0.18F,
            0.68F,
            0.82F,
            near_detail ? 3 : 1);
        break;
    case MarineDecorKind::CoralFan: {
        const auto angle = instance.yaw_radians;
        const glm::vec3 right {
            std::cos(angle),
            0.0F,
            std::sin(angle),
        };
        const glm::vec3 normal {
            -right.z,
            0.0F,
            right.x,
        };
        const auto center = glm::vec3 {
            instance.position_x,
            instance.position_y,
            instance.position_z,
        };
        const auto half_width =
            instance.scale_x * 0.5F;
        append_quad(
            mesh,
            {{
                center - right * half_width,
                center + right * half_width,
                center + right * half_width +
                    glm::vec3 {0.0F, instance.scale_y, 0.0F},
                center - right * half_width +
                    glm::vec3 {0.0F, instance.scale_y, 0.0F},
            }},
            normal,
            instance.material,
            kTerrainSurfaceFlagUnderwaterSway);
        break;
    }
    case MarineDecorKind::BranchCoralWarm:
    case MarineDecorKind::BranchCoralLagoon:
        append_branch_coral(
            mesh,
            instance,
            near_detail);
        break;
    case MarineDecorKind::Shell:
        append_shell(mesh, instance);
        break;
    }
}

[[nodiscard]] auto finite_fish(
    const OceanLifeInstance& instance) noexcept -> bool {
    return std::isfinite(instance.position.x) &&
           std::isfinite(instance.position.y) &&
           std::isfinite(instance.position.z) &&
           std::isfinite(instance.scale) &&
           std::isfinite(instance.heading_radians) &&
           std::isfinite(instance.fade) &&
           instance.scale > kMinimumScale &&
           instance.fade > 0.001F;
}

[[nodiscard]] auto finite_matrix(
    const glm::mat4& matrix) noexcept -> bool {
    for (glm::length_t column = 0;
         column < 4;
         ++column) {
        for (glm::length_t row = 0;
             row < 4;
             ++row) {
            if (!std::isfinite(
                    matrix[column][row])) {
                return false;
            }
        }
    }
    return true;
}

void append_fish(
    OrganicTerrainMesh& mesh,
    const OceanLifeInstance& instance) {
    const auto fade_scale =
        std::sqrt(glm::clamp(instance.fade, 0.0F, 1.0F));
    const auto scale =
        instance.scale * fade_scale;
    if (!(scale > kMinimumScale)) {
        return;
    }
    const glm::vec3 forward {
        std::cos(instance.heading_radians),
        0.0F,
        std::sin(instance.heading_radians),
    };
    const glm::vec3 normal {
        -forward.z,
        0.0F,
        forward.x,
    };
    const auto half_length =
        scale *
        kFishHalfLengthScale;
    const auto half_height =
        scale *
        kFishHalfHeightScale;
    const auto center = instance.position;
    const auto base =
        static_cast<std::uint32_t>(mesh.vertices.size());
    const auto palette_light =
        static_cast<std::uint8_t>(
            ocean_life_instance_palette_index(instance) *
            5U);
    const auto flags =
        kTerrainSurfaceFlagDirectMaterial |
        kTerrainSurfaceFlagCutout |
        kTerrainSurfaceFlagMarineFish;
    const std::array<glm::vec3, 4U> positions {{
        center - forward * half_length -
            glm::vec3 {0.0F, half_height, 0.0F},
        center + forward * half_length -
            glm::vec3 {0.0F, half_height, 0.0F},
        center + forward * half_length +
            glm::vec3 {0.0F, half_height, 0.0F},
        center - forward * half_length +
            glm::vec3 {0.0F, half_height, 0.0F},
    }};
    constexpr std::array<glm::vec2, 4U> uvs {{
        {0.0F, 0.0F},
        {1.0F, 0.0F},
        {1.0F, 1.0F},
        {0.0F, 1.0F},
    }};
    for (std::size_t index = 0U;
         index < positions.size();
         ++index) {
        auto vertex =
            make_vertex(
                positions[index],
                normal,
                VisualMaterialId::ReefFish,
                uvs[index].x,
                uvs[index].y,
                true,
                kTerrainSurfaceFlagMarineFish,
                palette_light);
        vertex.surface_flags = flags;
        vertex.sky_light = 15U;
        mesh.vertices.push_back(vertex);
    }
    mesh.indices.insert(
        mesh.indices.end(),
        {
            base + 0U,
            base + 1U,
            base + 2U,
            base + 0U,
            base + 2U,
            base + 3U,
        });
    ++mesh.quad_count;
}

} // namespace

auto build_marine_decor_visual_mesh(
    std::span<const MarineDecorInstance> instances,
    const glm::vec3& camera_position,
    const MarineVisualBudget& budget) -> OrganicTerrainMesh {
    OrganicTerrainMesh mesh {};
    if (!std::isfinite(camera_position.x) ||
        !std::isfinite(camera_position.y) ||
        !std::isfinite(camera_position.z) ||
        !std::isfinite(budget.radius) ||
        !std::isfinite(budget.near_detail_radius) ||
        budget.radius <= 0.0F ||
        budget.maximum_instances == 0U) {
        return mesh;
    }

    const auto radius_squared =
        budget.radius * budget.radius;
    std::vector<DecorCandidate> candidates {};
    candidates.reserve(instances.size());
    for (const auto& instance : instances) {
        if (!finite_instance(instance)) {
            continue;
        }
        const auto dx =
            instance.position_x - camera_position.x;
        const auto dz =
            instance.position_z - camera_position.z;
        const auto distance_squared =
            dx * dx + dz * dz;
        if (!std::isfinite(distance_squared) ||
            distance_squared > radius_squared) {
            continue;
        }
        candidates.push_back({
            &instance,
            distance_squared,
            stable_instance_key(instance),
        });
    }
    std::sort(
        candidates.begin(),
        candidates.end(),
        [](const DecorCandidate& left,
           const DecorCandidate& right) noexcept {
            if (left.distance_squared !=
                right.distance_squared) {
                return left.distance_squared <
                       right.distance_squared;
            }
            return left.stable_key < right.stable_key;
        });

    const auto near_squared =
        std::max(budget.near_detail_radius, 0.0F) *
        std::max(budget.near_detail_radius, 0.0F);
    mesh.vertices.reserve(
        std::min(
            candidates.size(),
            budget.maximum_instances) *
        18U);
    mesh.indices.reserve(
        std::min(
            candidates.size(),
            budget.maximum_instances) *
        36U);
    std::size_t accepted = 0U;
    for (const auto& candidate : candidates) {
        const auto distance_ratio =
            std::sqrt(candidate.distance_squared) /
            budget.radius;
        const auto divisor =
            distance_ratio > 0.78F
                ? 4U
                : (distance_ratio > 0.52F ? 2U : 1U);
        if (divisor > 1U &&
            candidate.stable_key % divisor != 0U) {
            continue;
        }
        append_decor_instance(
            mesh,
            *candidate.instance,
            candidate.distance_squared <= near_squared);
        ++accepted;
        if (accepted >= budget.maximum_instances) {
            break;
        }
    }
    return mesh;
}

auto build_ocean_life_visual_mesh(
    std::span<const OceanLifeInstance> instances) -> OrganicTerrainMesh {
    OrganicTerrainMesh mesh {};
    const auto count =
        std::min(
            instances.size(),
            kOceanLifeMaximumInstanceCount);
    mesh.vertices.reserve(count * 4U);
    mesh.indices.reserve(count * 6U);
    for (std::size_t index = 0U;
         index < count;
         ++index) {
        if (finite_fish(instances[index])) {
            append_fish(mesh, instances[index]);
        }
    }
    return mesh;
}

auto ocean_life_instance_intersects_ship_protection(
    const OceanLifeInstance& instance,
    const glm::mat4& inverse_ship_model,
    const ShipProtectionProfile& protection_profile) noexcept -> bool {
    if (!finite_fish(instance) ||
        !finite_matrix(inverse_ship_model)) {
        return false;
    }

    const auto fade_scale =
        std::sqrt(
            glm::clamp(
                instance.fade,
                0.0F,
                1.0F));
    const auto scale =
        instance.scale *
        fade_scale;
    if (!(scale > kMinimumScale)) {
        return false;
    }

    const glm::vec3 forward {
        std::cos(instance.heading_radians),
        0.0F,
        std::sin(instance.heading_radians),
    };
    const glm::vec3 lateral {
        -forward.z,
        0.0F,
        forward.x,
    };
    constexpr glm::vec3 up {
        0.0F,
        1.0F,
        0.0F,
    };

    // Je reprends les dimensions exactes du quad et je lui ajoute une faible
    // épaisseur de sécurité. Le poisson disparaît donc avant que sa texture ne
    // puisse affleurer à travers une muraille en mouvement.
    const auto half_length =
        scale *
            kFishHalfLengthScale +
        0.10F;
    const auto half_height =
        scale *
            kFishHalfHeightScale +
        0.08F;
    const auto half_thickness =
        std::max(
            scale * 0.18F,
            0.08F);
    const auto& center =
        instance.position;
    const std::array<glm::vec3, 15U>
        sample_points {{
            center,
            center - forward * half_length,
            center + forward * half_length,
            center - up * half_height,
            center + up * half_height,
            center - lateral * half_thickness,
            center + lateral * half_thickness,
            center - forward * half_length -
                up * half_height,
            center - forward * half_length +
                up * half_height,
            center + forward * half_length -
                up * half_height,
            center + forward * half_length +
                up * half_height,
            center - forward * half_length -
                lateral * half_thickness,
            center - forward * half_length +
                lateral * half_thickness,
            center + forward * half_length -
                lateral * half_thickness,
            center + forward * half_length +
                lateral * half_thickness,
        }};

    for (const auto& world_point :
         sample_points) {
        const auto local_point =
            glm::vec3 {
                inverse_ship_model *
                glm::vec4 {
                    world_point,
                    1.0F,
                },
            };
        if (protection_profile
                .excludes_ocean_local(
                    local_point)) {
            return true;
        }
    }
    return false;
}

auto build_ocean_life_visual_mesh(
    std::span<const OceanLifeInstance> instances,
    const glm::mat4& inverse_ship_model,
    const ShipProtectionProfile& protection_profile) -> OrganicTerrainMesh {
    OrganicTerrainMesh mesh {};
    const auto count =
        std::min(
            instances.size(),
            kOceanLifeMaximumInstanceCount);
    mesh.vertices.reserve(count * 4U);
    mesh.indices.reserve(count * 6U);
    for (std::size_t index = 0U;
         index < count;
         ++index) {
        const auto& instance =
            instances[index];
        if (finite_fish(instance) &&
            !ocean_life_instance_intersects_ship_protection(
                instance,
                inverse_ship_model,
                protection_profile)) {
            append_fish(
                mesh,
                instance);
        }
    }
    return mesh;
}

} // namespace valcraft
