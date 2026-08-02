#include "render/BackroomsPropMesh.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <stdexcept>
#include <vector>

namespace valcraft {

namespace {

constexpr float kUvUnits = 256.0F;
constexpr float kNormalEpsilonSquared = 1.0e-12F;

struct PropVector {
    float x = 0.0F;
    float y = 0.0F;
    float z = 0.0F;
};

struct PropLight {
    std::uint8_t sky = 15U;
    std::uint8_t block = 0U;
};

[[nodiscard]] constexpr auto subtract(
    const PropVector& lhs,
    const PropVector& rhs) noexcept -> PropVector {
    return {lhs.x - rhs.x, lhs.y - rhs.y, lhs.z - rhs.z};
}

[[nodiscard]] constexpr auto cross(
    const PropVector& lhs,
    const PropVector& rhs) noexcept -> PropVector {
    return {
        lhs.y * rhs.z - lhs.z * rhs.y,
        lhs.z * rhs.x - lhs.x * rhs.z,
        lhs.x * rhs.y - lhs.y * rhs.x,
    };
}

[[nodiscard]] constexpr auto dot(
    const PropVector& lhs,
    const PropVector& rhs) noexcept -> float {
    return lhs.x * rhs.x + lhs.y * rhs.y + lhs.z * rhs.z;
}

[[nodiscard]] auto normalized(const PropVector& value) -> PropVector {
    const auto length_squared = dot(value, value);
    if (!std::isfinite(length_squared) ||
        length_squared <= kNormalEpsilonSquared) {
        throw std::invalid_argument(
            "La normale d'un accessoire Backrooms est invalide");
    }
    const auto inverse_length = 1.0F / std::sqrt(length_squared);
    return {
        value.x * inverse_length,
        value.y * inverse_length,
        value.z * inverse_length,
    };
}

[[nodiscard]] auto fixed_uv(float value) noexcept -> std::uint16_t {
    return static_cast<std::uint16_t>(std::round(
        std::clamp(value, 0.0F, 1.0F) * kUvUnits));
}

void include_point(
    ArchitecturalBounds& bounds,
    const PropVector& point) noexcept {
    if (!bounds.valid) {
        bounds = {
            point.x,
            point.y,
            point.z,
            point.x,
            point.y,
            point.z,
            true,
        };
        return;
    }
    bounds.min_x = std::min(bounds.min_x, point.x);
    bounds.min_y = std::min(bounds.min_y, point.y);
    bounds.min_z = std::min(bounds.min_z, point.z);
    bounds.max_x = std::max(bounds.max_x, point.x);
    bounds.max_y = std::max(bounds.max_y, point.y);
    bounds.max_z = std::max(bounds.max_z, point.z);
}

[[nodiscard]] auto neighbor_offset_for_normal(
    const PropVector& normal) noexcept -> BlockCoord {
    const auto absolute_x = std::abs(normal.x);
    const auto absolute_y = std::abs(normal.y);
    const auto absolute_z = std::abs(normal.z);
    if (absolute_y >= absolute_x && absolute_y >= absolute_z) {
        return {0, normal.y >= 0.0F ? 1 : -1, 0};
    }
    if (absolute_x >= absolute_z) {
        return {normal.x >= 0.0F ? 1 : -1, 0, 0};
    }
    return {0, 0, normal.z >= 0.0F ? 1 : -1};
}

[[nodiscard]] auto sample_light(
    const ArchitecturalSampler& sampler,
    BlockCoord owner,
    const PropVector& normal) -> PropLight {
    const auto offset = neighbor_offset_for_normal(normal);
    const auto owner_sample = sampler(owner.x, owner.y, owner.z);
    const auto neighbor_sample = sampler(
        owner.x + offset.x,
        owner.y + offset.y,
        owner.z + offset.z);
    return {
        std::min<std::uint8_t>(
            std::max(owner_sample.sky_light, neighbor_sample.sky_light),
            15U),
        std::min<std::uint8_t>(
            std::max(owner_sample.block_light, neighbor_sample.block_light),
            15U),
    };
}

void append_rounded_component(
    ArchitecturalMesh& mesh,
    const StylizedPrimitiveMesh& primitive,
    const ArchitecturalSampler& sampler,
    BlockCoord owner,
    BlockId material_block,
    const PropVector& local_center,
    const PropVector& scale) {
    if (primitive.empty() || primitive.indices.size() % 3U != 0U ||
        scale.x <= 0.0F || scale.y <= 0.0F || scale.z <= 0.0F) {
        throw std::invalid_argument(
            "La primitive d'un accessoire Backrooms est invalide");
    }
    if (mesh.vertices.size() >
        static_cast<std::size_t>(std::numeric_limits<std::uint32_t>::max()) -
            primitive.vertices.size()) {
        throw std::length_error(
            "Le maillage des accessoires Backrooms est trop grand");
    }

    const auto first_vertex =
        static_cast<std::uint32_t>(mesh.vertices.size());
    mesh.vertices.reserve(mesh.vertices.size() + primitive.vertices.size());
    for (const auto& source : primitive.vertices) {
        const PropVector position {
            static_cast<float>(owner.x) + local_center.x + source.x * scale.x,
            static_cast<float>(owner.y) + local_center.y + source.y * scale.y,
            static_cast<float>(owner.z) + local_center.z + source.z * scale.z,
        };
        const auto normal = normalized({
            source.nx / scale.x,
            source.ny / scale.y,
            source.nz / scale.z,
        });
        const auto light = sample_light(sampler, owner, normal);
        mesh.vertices.push_back({
            position.x,
            position.y,
            position.z,
            normal.x,
            normal.y,
            normal.z,
            fixed_uv(source.u),
            fixed_uv(source.v),
            material_block,
            light.sky,
            light.block,
            0U,
        });
        include_point(mesh.bounds, position);
    }

    mesh.indices.reserve(mesh.indices.size() + primitive.indices.size());
    for (const auto index : primitive.indices) {
        if (index >= primitive.vertices.size()) {
            throw std::logic_error(
                "La primitive Backrooms contient un index invalide");
        }
        mesh.indices.push_back(first_vertex + index);
    }
}

void append_desk(
    ArchitecturalMesh& mesh,
    const StylizedPrimitiveMesh& primitive,
    const ArchitecturalSampler& sampler,
    BlockCoord owner,
    BlockId material_block) {
    append_rounded_component(
        mesh, primitive, sampler, owner, material_block,
        {0.50F, 0.80F, 0.50F}, {0.94F, 0.14F, 0.70F});
    constexpr std::array<float, 2> kLegX {{0.17F, 0.83F}};
    constexpr std::array<float, 2> kLegZ {{0.23F, 0.77F}};
    for (const auto x : kLegX) {
        for (const auto z : kLegZ) {
            append_rounded_component(
                mesh, primitive, sampler, owner, material_block,
                {x, 0.40F, z}, {0.10F, 0.72F, 0.10F});
        }
    }
    append_rounded_component(
        mesh, primitive, sampler, owner, material_block,
        {0.50F, 0.59F, 0.79F}, {0.72F, 0.10F, 0.08F});
}

void append_chair(
    ArchitecturalMesh& mesh,
    const StylizedPrimitiveMesh& primitive,
    const ArchitecturalSampler& sampler,
    BlockCoord owner,
    BlockId material_block) {
    append_rounded_component(
        mesh, primitive, sampler, owner, material_block,
        {0.50F, 0.49F, 0.48F}, {0.64F, 0.14F, 0.62F});
    constexpr std::array<float, 2> kLegX {{0.27F, 0.73F}};
    constexpr std::array<float, 2> kLegZ {{0.27F, 0.69F}};
    for (const auto x : kLegX) {
        for (const auto z : kLegZ) {
            append_rounded_component(
                mesh, primitive, sampler, owner, material_block,
                {x, 0.25F, z}, {0.09F, 0.44F, 0.09F});
        }
    }
    append_rounded_component(
        mesh, primitive, sampler, owner, material_block,
        {0.50F, 0.75F, 0.75F}, {0.64F, 0.46F, 0.10F});
}

[[nodiscard]] auto same_position(
    const PropVector& lhs,
    const PropVector& rhs) noexcept -> bool {
    return lhs.x == rhs.x && lhs.y == rhs.y && lhs.z == rhs.z;
}

[[nodiscard]] auto projected_uv(
    const PropVector& point,
    const PropVector& normal) noexcept -> std::array<float, 2> {
    if (std::abs(normal.y) >= std::abs(normal.x) &&
        std::abs(normal.y) >= std::abs(normal.z)) {
        return {point.x, point.z};
    }
    if (std::abs(normal.x) >= std::abs(normal.z)) {
        return {point.z, point.y};
    }
    return {point.x, point.y};
}

void append_polygon(
    ArchitecturalMesh& mesh,
    const ArchitecturalSampler& sampler,
    BlockCoord owner,
    BlockId material_block,
    std::vector<PropVector> local_positions,
    const PropVector& expected_normal) {
    std::vector<PropVector> compacted;
    compacted.reserve(local_positions.size());
    for (const auto& position : local_positions) {
        if (compacted.empty() ||
            !same_position(compacted.back(), position)) {
            compacted.push_back(position);
        }
    }
    if (compacted.size() > 1U &&
        same_position(compacted.front(), compacted.back())) {
        compacted.pop_back();
    }
    if (compacted.size() < 3U) {
        return;
    }

    const auto normal = normalized(expected_normal);
    const auto geometric_normal = cross(
        subtract(compacted[1], compacted[0]),
        subtract(compacted[2], compacted[0]));
    if (dot(geometric_normal, normal) < 0.0F) {
        std::reverse(compacted.begin(), compacted.end());
    }
    if (mesh.vertices.size() >
        static_cast<std::size_t>(std::numeric_limits<std::uint32_t>::max()) -
            compacted.size()) {
        throw std::length_error(
            "Le prisme Backrooms depasse la plage de ses indices");
    }

    const auto first_vertex =
        static_cast<std::uint32_t>(mesh.vertices.size());
    const auto light = sample_light(sampler, owner, normal);
    for (const auto& local : compacted) {
        const PropVector position {
            static_cast<float>(owner.x) + local.x,
            static_cast<float>(owner.y) + local.y,
            static_cast<float>(owner.z) + local.z,
        };
        const auto uv = projected_uv(local, normal);
        mesh.vertices.push_back({
            position.x,
            position.y,
            position.z,
            normal.x,
            normal.y,
            normal.z,
            fixed_uv(uv[0]),
            fixed_uv(uv[1]),
            material_block,
            light.sky,
            light.block,
            0U,
        });
        include_point(mesh.bounds, position);
    }
    for (std::uint32_t index = 1U;
         index + 1U < compacted.size();
         ++index) {
        mesh.indices.push_back(first_vertex);
        mesh.indices.push_back(first_vertex + index);
        mesh.indices.push_back(first_vertex + index + 1U);
    }
}

void append_ramp(
    ArchitecturalMesh& mesh,
    const ArchitecturalSampler& sampler,
    BlockCoord owner,
    BlockId material_block) {
    const auto height = [material_block](float x, float z) noexcept {
        return backrooms_ramp_surface_height(material_block, x, z);
    };
    const auto rise = backrooms_ramp_rise_direction(material_block);

    const auto neighbor_is_opaque = [&sampler, owner](BlockCoord offset) {
        return is_block_opaque(sampler(
            owner.x + offset.x,
            owner.y + offset.y,
            owner.z + offset.z).block_id);
    };
    if (!neighbor_is_opaque({0, -1, 0})) {
        append_polygon(
            mesh, sampler, owner, material_block,
            {{0.0F, 0.0F, 0.0F}, {0.0F, 0.0F, 1.0F},
             {1.0F, 0.0F, 1.0F}, {1.0F, 0.0F, 0.0F}},
            {0.0F, -1.0F, 0.0F});
    }

    append_polygon(
        mesh, sampler, owner, material_block,
        {{0.0F, height(0.0F, 0.0F), 0.0F},
         {1.0F, height(1.0F, 0.0F), 0.0F},
         {1.0F, height(1.0F, 1.0F), 1.0F},
         {0.0F, height(0.0F, 1.0F), 1.0F}},
        normalized({
            -static_cast<float>(rise.x),
            1.0F,
            -static_cast<float>(rise.z),
        }));

    struct Side {
        BlockCoord offset;
        PropVector normal;
        std::array<PropVector, 4> points;
    };
    const std::array<Side, 4> sides {{
        {{1, 0, 0}, {1.0F, 0.0F, 0.0F},
         {{{1.0F, 0.0F, 0.0F}, {1.0F, 0.0F, 1.0F},
           {1.0F, height(1.0F, 1.0F), 1.0F},
           {1.0F, height(1.0F, 0.0F), 0.0F}}}},
        {{-1, 0, 0}, {-1.0F, 0.0F, 0.0F},
         {{{0.0F, 0.0F, 1.0F}, {0.0F, 0.0F, 0.0F},
           {0.0F, height(0.0F, 0.0F), 0.0F},
           {0.0F, height(0.0F, 1.0F), 1.0F}}}},
        {{0, 0, 1}, {0.0F, 0.0F, 1.0F},
         {{{1.0F, 0.0F, 1.0F}, {0.0F, 0.0F, 1.0F},
           {0.0F, height(0.0F, 1.0F), 1.0F},
           {1.0F, height(1.0F, 1.0F), 1.0F}}}},
        {{0, 0, -1}, {0.0F, 0.0F, -1.0F},
         {{{0.0F, 0.0F, 0.0F}, {1.0F, 0.0F, 0.0F},
           {1.0F, height(1.0F, 0.0F), 0.0F},
           {0.0F, height(0.0F, 0.0F), 0.0F}}}},
    }};

    for (const auto& side : sides) {
        const auto neighbor = sampler(
            owner.x + side.offset.x,
            owner.y,
            owner.z + side.offset.z).block_id;
        const auto parallel_to_rise =
            rise.x * side.offset.x + rise.z * side.offset.z != 0;
        if (is_block_opaque(neighbor) ||
            (!parallel_to_rise && neighbor == material_block)) {
            continue;
        }
        append_polygon(
            mesh,
            sampler,
            owner,
            material_block,
            std::vector<PropVector>(side.points.begin(), side.points.end()),
            side.normal);
    }
}

void append_prop(
    ArchitecturalMesh& mesh,
    const StylizedPrimitiveMesh& rounded_box,
    const ArchitecturalSampler& sampler,
    BlockCoord owner,
    BlockId block_id) {
    switch (static_cast<BlockType>(block_id)) {
    case BlockType::BackroomsDesk:
        append_desk(mesh, rounded_box, sampler, owner, block_id);
        return;
    case BlockType::BackroomsChair:
        append_chair(mesh, rounded_box, sampler, owner, block_id);
        return;
    case BlockType::BackroomsRampPositiveX:
    case BlockType::BackroomsRampNegativeX:
    case BlockType::BackroomsRampPositiveZ:
    case BlockType::BackroomsRampNegativeZ:
        append_ramp(mesh, sampler, owner, block_id);
        return;
    default:
        throw std::invalid_argument(
            "Le bloc n'est pas un accessoire Backrooms hard-surface");
    }
}

} // namespace

auto append_modern_backrooms_prop_geometry(
    ArchitecturalMesh& mesh,
    const ArchitecturalSection& section,
    const ArchitecturalSampler& sampler,
    StylizedPrimitiveLod lod) -> std::size_t {
    const auto first_added_index = mesh.indices.size();
    if (!section.valid() || !sampler) {
        throw std::invalid_argument(
            "La section d'accessoires Backrooms est invalide");
    }

    std::optional<StylizedPrimitiveMesh> rounded_box;
    for (int y = section.min.y; y <= section.max.y; ++y) {
        for (int z = section.min.z; z <= section.max.z; ++z) {
            for (int x = section.min.x; x <= section.max.x; ++x) {
                const auto sample = sampler(x, y, z);
                if (!is_modern_backrooms_hard_surface_prop(sample.block_id)) {
                    continue;
                }
                if (is_backrooms_ramp(sample.block_id)) {
                    append_ramp(
                        mesh,
                        sampler,
                        {x, y, z},
                        sample.block_id);
                    continue;
                }
                if (!rounded_box.has_value()) {
                    // Je ne construis la primitive arrondie que si la section
                    // contient reellement un meuble. Les grandes
                    // sections architecturales et les rampes seules ne paient
                    // donc aucune tessellation inutile.
                    rounded_box.emplace(build_stylized_rounded_box(lod));
                }
                append_prop(
                    mesh,
                    *rounded_box,
                    sampler,
                    {x, y, z},
                    sample.block_id);
            }
        }
    }
    return first_added_index;
}

} // namespace valcraft
