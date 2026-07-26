#include "render/VisualVegetation.h"

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <stdexcept>
#include <utility>

namespace valcraft {

namespace {

constexpr float kPi = 3.14159265358979323846F;
constexpr std::uint64_t kFnvOffset = 14695981039346656037ULL;
constexpr std::uint64_t kFnvPrime = 1099511628211ULL;
constexpr std::size_t kMaximumSampledCells = 4U * 1024U * 1024U;

struct SampledVolume {
    BlockCoord min {};
    BlockCoord max {};
    std::size_t size_x = 0;
    std::size_t size_y = 0;
    std::size_t size_z = 0;
    std::vector<BlockId> blocks;

    [[nodiscard]] auto contains(BlockCoord coordinate) const noexcept -> bool {
        return coordinate.x >= min.x && coordinate.x <= max.x &&
               coordinate.y >= min.y && coordinate.y <= max.y &&
               coordinate.z >= min.z && coordinate.z <= max.z;
    }

    [[nodiscard]] auto index(BlockCoord coordinate) const noexcept -> std::size_t {
        const auto local_x = static_cast<std::size_t>(coordinate.x - min.x);
        const auto local_y = static_cast<std::size_t>(coordinate.y - min.y);
        const auto local_z = static_cast<std::size_t>(coordinate.z - min.z);
        return (local_y * size_z + local_z) * size_x + local_x;
    }

    [[nodiscard]] auto get(BlockCoord coordinate) const noexcept -> BlockId {
        return blocks[index(coordinate)];
    }
};

[[nodiscard]] auto is_inside_core(
    const VisualVegetationSection& section,
    BlockCoord coordinate) noexcept -> bool {
    return coordinate.x >= section.min.x && coordinate.x <= section.max.x &&
           coordinate.y >= section.min.y && coordinate.y <= section.max.y &&
           coordinate.z >= section.min.z && coordinate.z <= section.max.z;
}

[[nodiscard]] auto is_wood(BlockId block) noexcept -> bool {
    return block == to_block_id(BlockType::Wood) ||
           block == to_block_id(BlockType::PineWood);
}

[[nodiscard]] auto matching_leaf(BlockId wood) noexcept -> BlockId {
    return wood == to_block_id(BlockType::PineWood)
        ? to_block_id(BlockType::PineLeaves)
        : to_block_id(BlockType::Leaves);
}

[[nodiscard]] auto checked_extent(int minimum, int maximum) -> std::size_t {
    const auto extent = static_cast<std::int64_t>(maximum) -
                        static_cast<std::int64_t>(minimum) + 1;
    if (extent <= 0) {
        throw std::invalid_argument("visual vegetation section has invalid bounds");
    }
    return static_cast<std::size_t>(extent);
}

[[nodiscard]] auto checked_expanded_coordinate(int value, int delta) -> int {
    const auto result = static_cast<std::int64_t>(value) +
                        static_cast<std::int64_t>(delta);
    if (result < static_cast<std::int64_t>(std::numeric_limits<int>::min()) ||
        result > static_cast<std::int64_t>(std::numeric_limits<int>::max())) {
        throw std::overflow_error("visual vegetation halo exceeds integer coordinates");
    }
    return static_cast<int>(result);
}

[[nodiscard]] auto sample_volume(
    const VisualVegetationSection& section,
    const VisualVegetationSampler& sampler) -> SampledVolume {
    if (!sampler) {
        throw std::invalid_argument("visual vegetation sampler is empty");
    }
    if (section.min.x > section.max.x ||
        section.min.y > section.max.y ||
        section.min.z > section.max.z) {
        throw std::invalid_argument("visual vegetation section has invalid bounds");
    }
    if (section.halo < 1 || section.halo > 8) {
        throw std::invalid_argument("visual vegetation halo must be between one and eight");
    }

    SampledVolume volume {};
    volume.min = {
        checked_expanded_coordinate(section.min.x, -section.halo),
        checked_expanded_coordinate(section.min.y, -section.halo),
        checked_expanded_coordinate(section.min.z, -section.halo),
    };
    volume.max = {
        checked_expanded_coordinate(section.max.x, section.halo),
        checked_expanded_coordinate(section.max.y, section.halo),
        checked_expanded_coordinate(section.max.z, section.halo),
    };
    volume.size_x = checked_extent(volume.min.x, volume.max.x);
    volume.size_y = checked_extent(volume.min.y, volume.max.y);
    volume.size_z = checked_extent(volume.min.z, volume.max.z);

    if (volume.size_x > kMaximumSampledCells / volume.size_y ||
        volume.size_x * volume.size_y > kMaximumSampledCells / volume.size_z) {
        throw std::length_error("visual vegetation section is too large");
    }
    const auto cell_count = volume.size_x * volume.size_y * volume.size_z;
    volume.blocks.reserve(cell_count);
    for (int y = volume.min.y; y <= volume.max.y; ++y) {
        for (int z = volume.min.z; z <= volume.max.z; ++z) {
            for (int x = volume.min.x; x <= volume.max.x; ++x) {
                volume.blocks.push_back(sampler(x, y, z));
            }
        }
    }
    return volume;
}

[[nodiscard]] auto less_coordinate(BlockCoord lhs, BlockCoord rhs) noexcept -> bool {
    if (lhs.y != rhs.y) {
        return lhs.y < rhs.y;
    }
    if (lhs.x != rhs.x) {
        return lhs.x < rhs.x;
    }
    return lhs.z < rhs.z;
}

[[nodiscard]] auto logical_bounds_of(
    const std::vector<BlockCoord>& cells,
    const std::vector<BlockCoord>& foliage) -> VisualVegetationBounds {
    VisualVegetationBounds bounds {};
    const auto include_cell = [&bounds](BlockCoord cell) {
        const auto min_x = static_cast<float>(cell.x);
        const auto min_y = static_cast<float>(cell.y);
        const auto min_z = static_cast<float>(cell.z);
        const auto max_x = min_x + 1.0F;
        const auto max_y = min_y + 1.0F;
        const auto max_z = min_z + 1.0F;
        if (!bounds.valid) {
            bounds = {min_x, min_y, min_z, max_x, max_y, max_z, true};
            return;
        }
        bounds.min_x = std::min(bounds.min_x, min_x);
        bounds.min_y = std::min(bounds.min_y, min_y);
        bounds.min_z = std::min(bounds.min_z, min_z);
        bounds.max_x = std::max(bounds.max_x, max_x);
        bounds.max_y = std::max(bounds.max_y, max_y);
        bounds.max_z = std::max(bounds.max_z, max_z);
    };
    for (const auto cell : cells) {
        include_cell(cell);
    }
    for (const auto cell : foliage) {
        include_cell(cell);
    }
    return bounds;
}

void include_bounds(VisualVegetationBounds& destination, const VisualVegetationBounds& source) {
    if (!source.valid) {
        return;
    }
    if (!destination.valid) {
        destination = source;
        return;
    }
    destination.min_x = std::min(destination.min_x, source.min_x);
    destination.min_y = std::min(destination.min_y, source.min_y);
    destination.min_z = std::min(destination.min_z, source.min_z);
    destination.max_x = std::max(destination.max_x, source.max_x);
    destination.max_y = std::max(destination.max_y, source.max_y);
    destination.max_z = std::max(destination.max_z, source.max_z);
}

[[nodiscard]] auto make_bounds(
    float center_x,
    float center_y,
    float center_z,
    float scale_x,
    float scale_y,
    float scale_z,
    float yaw_radians) noexcept -> VisualVegetationBounds {
    const auto cosine = std::abs(std::cos(yaw_radians));
    const auto sine = std::abs(std::sin(yaw_radians));
    // Je conserve des bornes exactes pour une primitive canonique tournee :
    // une couronne anisotrope ne sort jamais de son volume de culling.
    const auto half_x = (cosine * scale_x + sine * scale_z) * 0.5F;
    const auto half_y = scale_y * 0.5F;
    const auto half_z = (sine * scale_x + cosine * scale_z) * 0.5F;
    return {
        center_x - half_x,
        center_y - half_y,
        center_z - half_z,
        center_x + half_x,
        center_y + half_y,
        center_z + half_z,
        true,
    };
}

[[nodiscard]] auto mix32(std::uint32_t value) noexcept -> std::uint32_t {
    value ^= value >> 16U;
    value *= 0x7FEB352DU;
    value ^= value >> 15U;
    value *= 0x846CA68BU;
    value ^= value >> 16U;
    return value;
}

[[nodiscard]] auto unit_float(std::uint32_t value) noexcept -> float {
    return static_cast<float>(value & 0xFFFFU) / 65535.0F;
}

[[nodiscard]] auto signed_float(std::uint32_t value) noexcept -> float {
    return unit_float(value) * 2.0F - 1.0F;
}

[[nodiscard]] auto instance_seed(
    const VisualVegetationSource& source,
    VisualVegetationLod lod,
    std::uint32_t ordinal) noexcept -> std::uint32_t {
    return mix32(
        source.seed ^
        (static_cast<std::uint32_t>(lod) * 0x9E3779B9U) ^
        (ordinal * 0x85EBCA6BU));
}

void append_instance(
    VisualVegetationLodBatch& batch,
    const VisualVegetationSource& source,
    VisualVegetationPrimitive primitive,
    float center_x,
    float center_y,
    float center_z,
    float scale_x,
    float scale_y,
    float scale_z,
    std::uint32_t ordinal,
    std::optional<float> yaw_override = std::nullopt) {
    const auto seed = instance_seed(source, batch.lod, ordinal);
    VisualVegetationInstance instance {};
    instance.position_x = center_x;
    instance.position_y = center_y;
    instance.position_z = center_z;
    instance.scale_x = scale_x;
    instance.scale_y = scale_y;
    instance.scale_z = scale_z;
    instance.yaw_radians = yaw_override.value_or(
        unit_float(mix32(seed ^ 0xA341316CU)) * (2.0F * kPi));
    instance.wind_phase = unit_float(mix32(seed ^ 0xC8013EA4U)) * (2.0F * kPi);
    instance.bounds = make_bounds(
        center_x,
        center_y,
        center_z,
        scale_x,
        scale_y,
        scale_z,
        instance.yaw_radians);
    instance.seed = seed;
    instance.primitive = primitive;
    instance.source_kind = source.kind;
    const auto canopy =
        primitive == VisualVegetationPrimitive::EllipsoidCanopy ||
        primitive == VisualVegetationPrimitive::ConicalCanopy ||
        primitive == VisualVegetationPrimitive::SimplifiedBouquet ||
        primitive == VisualVegetationPrimitive::Impostor ||
        primitive == VisualVegetationPrimitive::LeafSpray;
    if (canopy &&
        source.kind == VisualVegetationSourceKind::BroadleafTree) {
        instance.material_block = to_block_id(BlockType::Leaves);
    } else if (
        canopy &&
        source.kind == VisualVegetationSourceKind::PineTree) {
        instance.material_block = to_block_id(BlockType::PineLeaves);
    } else {
        instance.material_block = source.source_block;
    }
    batch.instances.push_back(instance);
    include_bounds(batch.bounds, instance.bounds);
}

[[nodiscard]] auto trunk_height(const VisualVegetationSource& source) noexcept -> float {
    if (source.source_bounds.valid) {
        return std::max(
            1.0F,
            source.source_bounds.max_y - source.source_bounds.min_y);
    }
    return std::max(
        1.0F,
        static_cast<float>(source.logical_bounds.max_y) -
            static_cast<float>(source.anchor.y));
}

[[nodiscard]] auto usable_foliage_bounds(
    const VisualVegetationSource& source) noexcept
    -> VisualVegetationBounds {
    if (source.foliage_bounds.valid) {
        return source.foliage_bounds;
    }
    return source.logical_bounds;
}

[[nodiscard]] auto bounds_extent(
    float minimum,
    float maximum,
    float fallback) noexcept -> float {
    const auto extent = maximum - minimum;
    return std::isfinite(extent) && extent > 0.0F
        ? extent
        : fallback;
}

void append_leaf_sprays(
    VisualVegetationLodBatch& batch,
    const VisualVegetationSource& source,
    float canopy_x,
    float canopy_y,
    float canopy_z,
    float canopy_width,
    float canopy_height,
    float canopy_depth) {
    const auto base_angle =
        unit_float(mix32(source.seed ^ 0x6D2B79F5U)) * (2.0F * kPi);
    constexpr std::array<float, 2> kAngleOffsets {{
        0.0F,
        2.42F,
    }};
    constexpr std::array<float, 2> kHeightOffsets {{
        0.13F,
        -0.09F,
    }};
    const auto spray_width = std::clamp(
        std::min(canopy_width, canopy_depth) * 0.35F,
        0.82F,
        1.16F);
    const auto spray_height = std::clamp(
        canopy_height * 0.46F,
        0.82F,
        1.28F);

    for (std::uint32_t index = 0U;
         index < kAngleOffsets.size();
         ++index) {
        const auto angle = base_angle + kAngleOffsets[index];
        const auto radial_scale = index == 0U ? 0.47F : 0.44F;
        // Je place chaque panneau tangentiellement à la couronne. Son alpha
        // reprend le matériau de feuilles et ne dessine donc que quelques
        // accents périphériques, sans créer un rectangle visible.
        append_instance(
            batch,
            source,
            VisualVegetationPrimitive::LeafSpray,
            canopy_x + std::cos(angle) * canopy_width * radial_scale,
            canopy_y + kHeightOffsets[index] * canopy_height,
            canopy_z + std::sin(angle) * canopy_depth * radial_scale,
            spray_width,
            spray_height,
            0.04F,
            16U + index,
            kPi * 0.5F - angle);
    }
}

void append_tree_instances(
    VisualVegetationLodBatch& batch,
    const VisualVegetationSource& source) {
    const auto base_x = static_cast<float>(source.anchor.x) + 0.5F;
    const auto base_y = source.source_bounds.valid
        ? source.source_bounds.min_y
        : static_cast<float>(source.anchor.y);
    const auto base_z = static_cast<float>(source.anchor.z) + 0.5F;
    const auto height = trunk_height(source);
    const auto pine = source.kind == VisualVegetationSourceKind::PineTree;
    const auto foliage = usable_foliage_bounds(source);
    const auto foliage_width = bounds_extent(
        foliage.min_x,
        foliage.max_x,
        pine ? 3.4F : 3.2F);
    const auto foliage_height = bounds_extent(
        foliage.min_y,
        foliage.max_y,
        pine ? 3.8F : 2.1F);
    const auto foliage_depth = bounds_extent(
        foliage.min_z,
        foliage.max_z,
        pine ? 3.4F : 3.2F);
    const auto canopy_x = (foliage.min_x + foliage.max_x) * 0.5F;
    const auto canopy_y = (foliage.min_y + foliage.max_y) * 0.5F;
    const auto canopy_z = (foliage.min_z + foliage.max_z) * 0.5F;
    const auto canopy_width = std::clamp(
        foliage_width * (pine ? 1.04F : 1.08F),
        pine ? 2.8F : 2.6F,
        pine ? 4.2F : 3.9F);
    const auto canopy_height = std::clamp(
        foliage_height * (pine ? 1.06F : 1.08F),
        pine ? 2.8F : 1.9F,
        pine ? 4.6F : 2.7F);
    const auto canopy_depth = std::clamp(
        foliage_depth * (pine ? 1.04F : 1.06F),
        pine ? 2.8F : 2.6F,
        pine ? 4.2F : 3.9F);

    if (batch.lod == VisualVegetationLod::Far) {
        const auto visual_width =
            std::max(canopy_width, canopy_depth) * 1.04F;
        const auto visual_height = std::max(
            4.0F,
            source.logical_bounds.max_y - source.logical_bounds.min_y);
        append_instance(
            batch,
            source,
            VisualVegetationPrimitive::Impostor,
            (source.logical_bounds.min_x + source.logical_bounds.max_x) * 0.5F,
            (source.logical_bounds.min_y + source.logical_bounds.max_y) * 0.5F,
            (source.logical_bounds.min_z + source.logical_bounds.max_z) * 0.5F,
            visual_width,
            visual_height,
            0.05F,
            0U);
        return;
    }

    // Je garde la base et la hauteur de la colonne logique, mais j'assume un
    // diamètre visuel légèrement inférieur à la cellule. La collision reste
    // cellulaire et invisible, tandis que le tronc cesse d'être un pilier.
    const auto trunk_width = pine ? 0.84F : 0.90F;
    append_instance(
        batch,
        source,
        VisualVegetationPrimitive::TaperedTrunk,
        base_x,
        base_y + height * 0.5F,
        base_z,
        trunk_width,
        height,
        trunk_width,
        0U);

    if (batch.lod == VisualVegetationLod::Medium && pine) {
        for (std::uint32_t layer = 0U; layer < 3U; ++layer) {
            const auto layer_float = static_cast<float>(layer);
            const auto width_factor = 1.0F - layer_float * 0.25F;
            append_instance(
                batch,
                source,
                VisualVegetationPrimitive::ConicalCanopy,
                canopy_x,
                canopy_y +
                    (layer_float - 1.0F) * canopy_height * 0.28F,
                canopy_z,
                canopy_width * width_factor,
                canopy_height * 0.46F,
                canopy_depth * width_factor,
                layer + 1U);
        }
        append_leaf_sprays(
            batch,
            source,
            canopy_x,
            canopy_y,
            canopy_z,
            canopy_width,
            canopy_height,
            canopy_depth);
        return;
    }

    if (pine) {
        for (std::uint32_t layer = 0U; layer < 3U; ++layer) {
            const auto layer_float = static_cast<float>(layer);
            const auto width_factor = 1.0F - layer_float * 0.25F;
            const auto layer_height = canopy_height * 0.46F;
            append_instance(
                batch,
                source,
                VisualVegetationPrimitive::ConicalCanopy,
                canopy_x,
                canopy_y +
                    (layer_float - 1.0F) * canopy_height * 0.28F,
                canopy_z,
                canopy_width * width_factor,
                layer_height,
                canopy_depth * width_factor,
                layer + 1U);
        }
        append_leaf_sprays(
            batch,
            source,
            canopy_x,
            canopy_y,
            canopy_z,
            canopy_width,
            canopy_height,
            canopy_depth);
        return;
    }

    struct BroadleafLobe {
        std::array<float, 3> offset {};
        std::array<float, 3> scale {};
    };
    constexpr std::array<BroadleafLobe, 3> kMediumLobes {{
        {{{-0.20F, -0.05F, 0.02F}}, {{0.74F, 0.66F, 0.58F}}},
        {{{0.15F, -0.04F, 0.14F}}, {{0.58F, 0.80F, 0.75F}}},
        {{{0.03F, 0.24F, -0.16F}}, {{0.67F, 0.58F, 0.63F}}},
    }};
    constexpr std::array<BroadleafLobe, 3> kNearLobes {{
        {{{-0.21F, -0.06F, 0.03F}}, {{0.78F, 0.72F, 0.62F}}},
        {{{0.17F, -0.03F, 0.15F}}, {{0.64F, 0.84F, 0.78F}}},
        {{{0.03F, 0.25F, -0.17F}}, {{0.71F, 0.64F, 0.68F}}},
    }};
    const auto& lobes = batch.lod == VisualVegetationLod::Medium
        ? kMediumLobes
        : kNearLobes;
    for (std::uint32_t index = 0U; index < lobes.size(); ++index) {
        const auto& lobe = lobes[index];
        const auto jitter =
            signed_float(mix32(source.seed ^ (index * 0x27D4EB2DU))) *
            std::min(canopy_width, canopy_depth) * 0.025F;
        append_instance(
            batch,
            source,
            VisualVegetationPrimitive::EllipsoidCanopy,
            canopy_x + lobe.offset[0] * canopy_width + jitter,
            canopy_y + lobe.offset[1] * canopy_height,
            canopy_z + lobe.offset[2] * canopy_depth - jitter,
            canopy_width * lobe.scale[0],
            canopy_height * lobe.scale[1],
            canopy_depth * lobe.scale[2],
            index + 1U);
    }
    append_leaf_sprays(
        batch,
        source,
        canopy_x,
        canopy_y,
        canopy_z,
        canopy_width,
        canopy_height,
        canopy_depth);
}

void append_cactus_instances(
    VisualVegetationLodBatch& batch,
    const VisualVegetationSource& source) {
    const auto center_x = static_cast<float>(source.anchor.x) + 0.5F;
    const auto base_y = static_cast<float>(source.anchor.y);
    const auto center_z = static_cast<float>(source.anchor.z) + 0.5F;
    const auto height = std::max(
        1.0F,
        static_cast<float>(source.source_cell_count));
    if (batch.lod == VisualVegetationLod::Far) {
        append_instance(
            batch,
            source,
            VisualVegetationPrimitive::Impostor,
            center_x,
            base_y + height * 0.5F,
            center_z,
            1.55F,
            height,
            0.05F,
            0U);
        return;
    }

    append_instance(
        batch,
        source,
        VisualVegetationPrimitive::CactusStem,
        center_x,
        base_y + height * 0.5F,
        center_z,
        0.74F,
        height,
        0.74F,
        0U);
    append_instance(
        batch,
        source,
        batch.lod == VisualVegetationLod::Medium
            ? VisualVegetationPrimitive::SimplifiedBouquet
            : VisualVegetationPrimitive::CactusArm,
        center_x + 0.45F,
        base_y + height * 0.58F,
        center_z,
        0.9F,
        0.44F,
        0.48F,
        1U);
    if (batch.lod == VisualVegetationLod::Near) {
        append_instance(
            batch,
            source,
            VisualVegetationPrimitive::CactusArm,
            center_x - 0.42F,
            base_y + height * 0.38F,
            center_z,
            0.84F,
            0.4F,
            0.46F,
            2U);
    }
}

void append_grass_instances(
    VisualVegetationLodBatch& batch,
    const VisualVegetationSource& source) {
    const auto seed_x = mix32(source.seed ^ 0xAD90777DU);
    const auto seed_z = mix32(source.seed ^ 0x7E95761EU);
    const auto center_x = static_cast<float>(source.anchor.x) + 0.5F +
                          signed_float(seed_x) * 0.08F;
    const auto base_y = static_cast<float>(source.anchor.y);
    const auto center_z = static_cast<float>(source.anchor.z) + 0.5F +
                          signed_float(seed_z) * 0.08F;
    if (batch.lod == VisualVegetationLod::Far) {
        append_instance(
            batch,
            source,
            VisualVegetationPrimitive::Impostor,
            center_x,
            base_y + 0.48F,
            center_z,
            0.9F,
            0.96F,
            0.04F,
            0U);
        return;
    }
    if (batch.lod == VisualVegetationLod::Medium) {
        for (std::uint32_t index = 0U; index < 2U; ++index) {
            append_instance(
                batch,
                source,
                VisualVegetationPrimitive::SimplifiedBouquet,
                center_x,
                base_y + 0.42F,
                center_z,
                0.75F,
                0.84F,
                0.75F,
                index);
        }
        return;
    }

    constexpr std::array<std::array<float, 2>, 5> kBladeOffsets {{
        {0.0F, 0.0F},
        {-0.20F, -0.12F},
        {0.18F, -0.09F},
        {-0.11F, 0.18F},
        {0.20F, 0.16F},
    }};
    for (std::uint32_t index = 0U; index < kBladeOffsets.size(); ++index) {
        append_instance(
            batch,
            source,
            VisualVegetationPrimitive::GrassBlade,
            center_x + kBladeOffsets[index][0],
            base_y + 0.42F,
            center_z + kBladeOffsets[index][1],
            0.18F,
            0.84F + 0.06F * static_cast<float>(index % 2U),
            0.05F,
            index);
    }
}

void append_flower_instances(
    VisualVegetationLodBatch& batch,
    const VisualVegetationSource& source) {
    const auto center_x = static_cast<float>(source.anchor.x) + 0.5F;
    const auto base_y = static_cast<float>(source.anchor.y);
    const auto center_z = static_cast<float>(source.anchor.z) + 0.5F;
    if (batch.lod == VisualVegetationLod::Far) {
        append_instance(
            batch,
            source,
            VisualVegetationPrimitive::Impostor,
            center_x,
            base_y + 0.5F,
            center_z,
            0.72F,
            1.0F,
            0.04F,
            0U);
        return;
    }

    append_instance(
        batch,
        source,
        VisualVegetationPrimitive::FlowerStem,
        center_x,
        base_y + 0.42F,
        center_z,
        0.09F,
        0.84F,
        0.09F,
        0U);
    if (batch.lod == VisualVegetationLod::Medium) {
        append_instance(
            batch,
            source,
            VisualVegetationPrimitive::SimplifiedBouquet,
            center_x,
            base_y + 0.84F,
            center_z,
            0.64F,
            0.32F,
            0.64F,
            1U);
        return;
    }

    constexpr std::array<std::array<float, 3>, 5> kPetalOffsets {{
        {0.0F, 0.04F, 0.0F},
        {-0.19F, 0.0F, 0.0F},
        {0.19F, 0.0F, 0.0F},
        {0.0F, 0.0F, -0.19F},
        {0.0F, 0.0F, 0.19F},
    }};
    for (std::uint32_t index = 0U; index < kPetalOffsets.size(); ++index) {
        const auto& offset = kPetalOffsets[index];
        append_instance(
            batch,
            source,
            VisualVegetationPrimitive::FlowerPetals,
            center_x + offset[0],
            base_y + 0.84F + offset[1],
            center_z + offset[2],
            index == 0U ? 0.24F : 0.3F,
            0.14F,
            index == 0U ? 0.24F : 0.3F,
            index + 1U);
    }
}

void append_source_lods(VisualVegetationBuild& build, const VisualVegetationSource& source) {
    if (!source.owns_instances ||
        source.kind == VisualVegetationSourceKind::WoodStructure ||
        source.kind == VisualVegetationSourceKind::PineStructure) {
        return;
    }
    for (auto& batch : build.lods) {
        switch (source.kind) {
        case VisualVegetationSourceKind::BroadleafTree:
        case VisualVegetationSourceKind::PineTree:
            append_tree_instances(batch, source);
            break;
        case VisualVegetationSourceKind::Cactus:
            append_cactus_instances(batch, source);
            break;
        case VisualVegetationSourceKind::TallGrass:
            append_grass_instances(batch, source);
            break;
        case VisualVegetationSourceKind::RedFlower:
        case VisualVegetationSourceKind::YellowFlower:
            append_flower_instances(batch, source);
            break;
        case VisualVegetationSourceKind::WoodStructure:
        case VisualVegetationSourceKind::PineStructure:
            break;
        }
    }
}

void classify_wood_components(
    const VisualVegetationSection& section,
    const SampledVolume& volume,
    std::uint32_t world_seed,
    VisualVegetationBuild& build) {
    std::vector<bool> visited(volume.blocks.size(), false);
    constexpr std::array<BlockCoord, 6> kNeighbours {{
        {-1, 0, 0},
        {1, 0, 0},
        {0, -1, 0},
        {0, 1, 0},
        {0, 0, -1},
        {0, 0, 1},
    }};

    for (int y = volume.min.y; y <= volume.max.y; ++y) {
        for (int z = volume.min.z; z <= volume.max.z; ++z) {
            for (int x = volume.min.x; x <= volume.max.x; ++x) {
                const BlockCoord start {x, y, z};
                const auto start_index = volume.index(start);
                const auto wood = volume.blocks[start_index];
                if (visited[start_index] || !is_wood(wood)) {
                    continue;
                }

                std::vector<BlockCoord> cells {};
                std::vector<BlockCoord> pending {start};
                visited[start_index] = true;
                for (std::size_t cursor = 0U; cursor < pending.size(); ++cursor) {
                    const auto current = pending[cursor];
                    cells.push_back(current);
                    for (const auto offset : kNeighbours) {
                        const BlockCoord neighbour {
                            current.x + offset.x,
                            current.y + offset.y,
                            current.z + offset.z,
                        };
                        if (!volume.contains(neighbour)) {
                            continue;
                        }
                        const auto neighbour_index = volume.index(neighbour);
                        if (!visited[neighbour_index] &&
                            volume.blocks[neighbour_index] == wood) {
                            visited[neighbour_index] = true;
                            pending.push_back(neighbour);
                        }
                    }
                }

                const auto has_core_cell = std::any_of(
                    cells.begin(),
                    cells.end(),
                    [&section](BlockCoord cell) {
                        return is_inside_core(section, cell);
                    });
                if (!has_core_cell) {
                    continue;
                }

                std::vector<BlockCoord> foliage {};
                const auto leaf = matching_leaf(wood);
                for (const auto cell : cells) {
                    for (int dy = -1; dy <= 1; ++dy) {
                        for (int dz = -1; dz <= 1; ++dz) {
                            for (int dx = -1; dx <= 1; ++dx) {
                                const BlockCoord candidate {
                                    cell.x + dx,
                                    cell.y + dy,
                                    cell.z + dz,
                                };
                                if (volume.contains(candidate) &&
                                    volume.get(candidate) == leaf) {
                                    foliage.push_back(candidate);
                                }
                            }
                        }
                    }
                }
                std::sort(foliage.begin(), foliage.end(), less_coordinate);
                foliage.erase(
                    std::unique(foliage.begin(), foliage.end()),
                    foliage.end());

                const auto representative = *std::min_element(
                    cells.begin(),
                    cells.end(),
                    less_coordinate);
                VisualVegetationSource source {};
                const auto pine = wood == to_block_id(BlockType::PineWood);
                const auto tree = !foliage.empty();
                source.kind = tree
                    ? (pine
                        ? VisualVegetationSourceKind::PineTree
                        : VisualVegetationSourceKind::BroadleafTree)
                    : (pine
                        ? VisualVegetationSourceKind::PineStructure
                        : VisualVegetationSourceKind::WoodStructure);
                source.source_block = wood;
                source.owns_instances = is_inside_core(section, representative);
                source.anchor = representative;
                source.source_cell_count = static_cast<std::uint32_t>(cells.size());
                source.foliage_cell_count = static_cast<std::uint32_t>(foliage.size());
                source.seed = visual_vegetation_seed(
                    world_seed,
                    representative,
                    pine ? 0x51ED270BU : 0xA17E93C5U);
                source.source_bounds = logical_bounds_of(cells, {});
                source.foliage_bounds = logical_bounds_of({}, foliage);
                source.logical_bounds = logical_bounds_of(cells, foliage);
                build.sources.push_back(source);
                append_source_lods(build, source);
            }
        }
    }
}

[[nodiscard]] auto decoration_kind(BlockId block) noexcept
    -> VisualVegetationSourceKind {
    if (block == to_block_id(BlockType::Cactus)) {
        return VisualVegetationSourceKind::Cactus;
    }
    if (block == to_block_id(BlockType::RedFlower)) {
        return VisualVegetationSourceKind::RedFlower;
    }
    if (block == to_block_id(BlockType::YellowFlower)) {
        return VisualVegetationSourceKind::YellowFlower;
    }
    return VisualVegetationSourceKind::TallGrass;
}

[[nodiscard]] auto is_single_cell_decoration(BlockId block) noexcept -> bool {
    return block == to_block_id(BlockType::TallGrass) ||
           block == to_block_id(BlockType::RedFlower) ||
           block == to_block_id(BlockType::YellowFlower);
}

void classify_decorations(
    const VisualVegetationSection& section,
    const SampledVolume& volume,
    std::uint32_t world_seed,
    VisualVegetationBuild& build) {
    for (int y = section.min.y; y <= section.max.y; ++y) {
        for (int z = section.min.z; z <= section.max.z; ++z) {
            for (int x = section.min.x; x <= section.max.x; ++x) {
                const BlockCoord coordinate {x, y, z};
                const auto block = volume.get(coordinate);
                if (block == to_block_id(BlockType::Cactus)) {
                    const BlockCoord below {x, y - 1, z};
                    if (volume.contains(below) &&
                        volume.get(below) == to_block_id(BlockType::Cactus)) {
                        continue;
                    }
                    std::uint32_t height = 1U;
                    for (int next_y = y + 1;
                         next_y <= volume.max.y &&
                         volume.get({x, next_y, z}) == to_block_id(BlockType::Cactus);
                         ++next_y) {
                        ++height;
                    }
                    VisualVegetationSource source {};
                    source.kind = VisualVegetationSourceKind::Cactus;
                    source.source_block = block;
                    source.owns_instances = true;
                    source.anchor = coordinate;
                    source.source_cell_count = height;
                    source.seed = visual_vegetation_seed(
                        world_seed,
                        coordinate,
                        0xCA7C0541U);
                    source.logical_bounds = {
                        static_cast<float>(x),
                        static_cast<float>(y),
                        static_cast<float>(z),
                        static_cast<float>(x + 1),
                        static_cast<float>(y) + static_cast<float>(height),
                        static_cast<float>(z + 1),
                        true,
                    };
                    source.source_bounds = source.logical_bounds;
                    build.sources.push_back(source);
                    append_source_lods(build, source);
                    continue;
                }
                if (!is_single_cell_decoration(block)) {
                    continue;
                }

                VisualVegetationSource source {};
                source.kind = decoration_kind(block);
                source.source_block = block;
                source.owns_instances = true;
                source.anchor = coordinate;
                source.source_cell_count = 1U;
                source.seed = visual_vegetation_seed(
                    world_seed,
                    coordinate,
                    0xF10A7E31U + static_cast<std::uint32_t>(block));
                source.logical_bounds = {
                    static_cast<float>(x),
                    static_cast<float>(y),
                    static_cast<float>(z),
                    static_cast<float>(x + 1),
                    static_cast<float>(y + 1),
                    static_cast<float>(z + 1),
                    true,
                };
                source.source_bounds = source.logical_bounds;
                build.sources.push_back(source);
                append_source_lods(build, source);
            }
        }
    }
}

void hash_byte(std::uint64_t& hash, std::uint8_t value) noexcept {
    hash ^= static_cast<std::uint64_t>(value);
    hash *= kFnvPrime;
}

void hash_u32(std::uint64_t& hash, std::uint32_t value) noexcept {
    for (unsigned int shift = 0U; shift < 32U; shift += 8U) {
        hash_byte(hash, static_cast<std::uint8_t>((value >> shift) & 0xFFU));
    }
}

void hash_u64(std::uint64_t& hash, std::uint64_t value) noexcept {
    for (unsigned int shift = 0U; shift < 64U; shift += 8U) {
        hash_byte(hash, static_cast<std::uint8_t>((value >> shift) & 0xFFULL));
    }
}

void hash_float(std::uint64_t& hash, float value) noexcept {
    hash_u32(hash, std::bit_cast<std::uint32_t>(value));
}

void hash_coordinate(std::uint64_t& hash, BlockCoord coordinate) noexcept {
    hash_u32(hash, static_cast<std::uint32_t>(coordinate.x));
    hash_u32(hash, static_cast<std::uint32_t>(coordinate.y));
    hash_u32(hash, static_cast<std::uint32_t>(coordinate.z));
}

void hash_bounds(std::uint64_t& hash, const VisualVegetationBounds& bounds) noexcept {
    hash_float(hash, bounds.min_x);
    hash_float(hash, bounds.min_y);
    hash_float(hash, bounds.min_z);
    hash_float(hash, bounds.max_x);
    hash_float(hash, bounds.max_y);
    hash_float(hash, bounds.max_z);
    hash_byte(hash, bounds.valid ? 1U : 0U);
}

} // namespace

auto visual_vegetation_lod_index(VisualVegetationLod lod) noexcept -> std::size_t {
    return static_cast<std::size_t>(lod);
}

auto visual_vegetation_seed(
    std::uint32_t world_seed,
    BlockCoord coordinate,
    std::uint32_t salt) noexcept -> std::uint32_t {
    auto hash = mix32(world_seed ^ salt ^ 0x9E3779B9U);
    hash = mix32(hash ^ static_cast<std::uint32_t>(coordinate.x));
    hash = mix32(hash ^ static_cast<std::uint32_t>(coordinate.y));
    hash = mix32(hash ^ static_cast<std::uint32_t>(coordinate.z));
    return hash;
}

auto build_visual_vegetation(
    const VisualVegetationSection& section,
    const VisualVegetationSampler& sampler,
    std::uint32_t world_seed) -> VisualVegetationBuild {
    const auto volume = sample_volume(section, sampler);
    VisualVegetationBuild build {};
    build.lods[visual_vegetation_lod_index(VisualVegetationLod::Near)].lod =
        VisualVegetationLod::Near;
    build.lods[visual_vegetation_lod_index(VisualVegetationLod::Medium)].lod =
        VisualVegetationLod::Medium;
    build.lods[visual_vegetation_lod_index(VisualVegetationLod::Far)].lod =
        VisualVegetationLod::Far;

    classify_wood_components(section, volume, world_seed, build);
    classify_decorations(section, volume, world_seed, build);
    for (const auto& batch : build.lods) {
        include_bounds(build.bounds, batch.bounds);
    }
    return build;
}

auto visual_vegetation_deterministic_hash(
    const VisualVegetationBuild& build) noexcept -> std::uint64_t {
    auto hash = kFnvOffset;
    hash_u64(hash, static_cast<std::uint64_t>(build.sources.size()));
    for (const auto& source : build.sources) {
        hash_byte(hash, static_cast<std::uint8_t>(source.kind));
        hash_byte(hash, source.source_block);
        hash_byte(hash, source.owns_instances ? 1U : 0U);
        hash_coordinate(hash, source.anchor);
        hash_u32(hash, source.source_cell_count);
        hash_u32(hash, source.foliage_cell_count);
        hash_u32(hash, source.seed);
        hash_bounds(hash, source.source_bounds);
        hash_bounds(hash, source.foliage_bounds);
        hash_bounds(hash, source.logical_bounds);
    }
    for (const auto& batch : build.lods) {
        hash_byte(hash, static_cast<std::uint8_t>(batch.lod));
        hash_u64(hash, static_cast<std::uint64_t>(batch.instances.size()));
        hash_bounds(hash, batch.bounds);
        for (const auto& instance : batch.instances) {
            hash_float(hash, instance.position_x);
            hash_float(hash, instance.position_y);
            hash_float(hash, instance.position_z);
            hash_float(hash, instance.scale_x);
            hash_float(hash, instance.scale_y);
            hash_float(hash, instance.scale_z);
            hash_float(hash, instance.yaw_radians);
            hash_float(hash, instance.wind_phase);
            hash_bounds(hash, instance.bounds);
            hash_u32(hash, instance.seed);
            hash_byte(hash, static_cast<std::uint8_t>(instance.primitive));
            hash_byte(hash, static_cast<std::uint8_t>(instance.source_kind));
            hash_byte(hash, instance.material_block);
        }
    }
    hash_bounds(hash, build.bounds);
    return hash;
}

} // namespace valcraft
