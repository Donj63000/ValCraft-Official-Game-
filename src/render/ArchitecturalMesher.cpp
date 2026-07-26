#include "render/ArchitecturalMesher.h"

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <stdexcept>

namespace valcraft {

namespace {

constexpr std::uint64_t kFnvOffset = 14695981039346656037ULL;
constexpr std::uint64_t kFnvPrime = 1099511628211ULL;
constexpr std::size_t kMaximumSampledCells = 4U * 1024U * 1024U;
constexpr std::uint16_t kUvUnitsPerBlock = 256U;

struct SampledVolume {
    BlockCoord min {};
    BlockCoord max {};
    std::size_t size_x = 0;
    std::size_t size_y = 0;
    std::size_t size_z = 0;
    std::vector<ArchitecturalCellSample> cells;

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

    [[nodiscard]] auto get(BlockCoord coordinate) const noexcept
        -> const ArchitecturalCellSample& {
        return cells[index(coordinate)];
    }
};

struct FaceDefinition {
    ArchitecturalFace face = ArchitecturalFace::PositiveX;
    int normal_axis = 0;
    int normal_sign = 1;
    int u_axis = 1;
    int v_axis = 2;
    std::array<float, 3> normal {1.0F, 0.0F, 0.0F};
};

// Je choisis U et V de sorte que U x V pointe toujours vers la normale. Les
// indices produits gardent donc le meme winding sur les six orientations.
constexpr std::array<FaceDefinition, 6> kFaceDefinitions {{
    {ArchitecturalFace::PositiveX, 0, 1, 1, 2, {1.0F, 0.0F, 0.0F}},
    {ArchitecturalFace::NegativeX, 0, -1, 2, 1, {-1.0F, 0.0F, 0.0F}},
    {ArchitecturalFace::PositiveY, 1, 1, 2, 0, {0.0F, 1.0F, 0.0F}},
    {ArchitecturalFace::NegativeY, 1, -1, 0, 2, {0.0F, -1.0F, 0.0F}},
    {ArchitecturalFace::PositiveZ, 2, 1, 0, 1, {0.0F, 0.0F, 1.0F}},
    {ArchitecturalFace::NegativeZ, 2, -1, 1, 0, {0.0F, 0.0F, -1.0F}},
}};

struct FaceMaskCell {
    bool visible = false;
    BlockId material_block = to_block_id(BlockType::Air);
    std::uint8_t sky_light = 15;
    std::uint8_t block_light = 0;

    auto operator==(const FaceMaskCell&) const -> bool = default;
};

[[nodiscard]] auto axis_value(BlockCoord coordinate, int axis) noexcept -> int {
    if (axis == 0) {
        return coordinate.x;
    }
    if (axis == 1) {
        return coordinate.y;
    }
    return coordinate.z;
}

void set_axis_value(BlockCoord& coordinate, int axis, int value) noexcept {
    if (axis == 0) {
        coordinate.x = value;
    } else if (axis == 1) {
        coordinate.y = value;
    } else {
        coordinate.z = value;
    }
}

[[nodiscard]] auto axis_min(const ArchitecturalSection& section, int axis) noexcept -> int {
    return axis_value(section.min, axis);
}

[[nodiscard]] auto axis_max(const ArchitecturalSection& section, int axis) noexcept -> int {
    return axis_value(section.max, axis);
}

[[nodiscard]] auto checked_expanded_coordinate(int value, int delta) -> int {
    const auto result = static_cast<std::int64_t>(value) +
                        static_cast<std::int64_t>(delta);
    if (result < static_cast<std::int64_t>(std::numeric_limits<int>::min()) ||
        result > static_cast<std::int64_t>(std::numeric_limits<int>::max())) {
        throw std::overflow_error("architectural halo exceeds integer coordinates");
    }
    return static_cast<int>(result);
}

[[nodiscard]] auto checked_extent(int minimum, int maximum) -> std::size_t {
    const auto extent = static_cast<std::int64_t>(maximum) -
                        static_cast<std::int64_t>(minimum) + 1;
    if (extent <= 0) {
        throw std::invalid_argument("architectural section has invalid bounds");
    }
    return static_cast<std::size_t>(extent);
}

[[nodiscard]] auto sample_volume(
    const ArchitecturalSection& section,
    const ArchitecturalSampler& sampler) -> SampledVolume {
    if (!sampler) {
        throw std::invalid_argument("architectural sampler is empty");
    }
    if (!section.valid()) {
        throw std::invalid_argument("architectural section has invalid bounds");
    }
    if (section.halo < 1 || section.halo > 8) {
        throw std::invalid_argument("architectural halo must be between one and eight");
    }

    for (int axis = 0; axis < 3; ++axis) {
        const auto core_extent = checked_extent(
            axis_min(section, axis),
            axis_max(section, axis));
        if (core_extent > 255U) {
            throw std::length_error("architectural section exceeds compact UV range");
        }
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
        throw std::length_error("architectural section is too large");
    }

    const auto cell_count = volume.size_x * volume.size_y * volume.size_z;
    volume.cells.reserve(cell_count);
    for (int y = volume.min.y; y <= volume.max.y; ++y) {
        for (int z = volume.min.z; z <= volume.max.z; ++z) {
            for (int x = volume.min.x; x <= volume.max.x; ++x) {
                auto sample = sampler(x, y, z);
                sample.sky_light = std::min<std::uint8_t>(sample.sky_light, 15U);
                sample.block_light = std::min<std::uint8_t>(sample.block_light, 15U);
                volume.cells.push_back(sample);
            }
        }
    }
    return volume;
}

[[nodiscard]] auto face_is_visible(BlockId block, BlockId neighbour) noexcept -> bool {
    if (!is_block_solid(neighbour)) {
        return true;
    }

    const auto glass = to_block_id(BlockType::Glass);
    if (block == glass) {
        // Je supprime aussi bien les faces verre-verre que la face du verre
        // collee a un materiau opaque.
        return false;
    }
    // Je conserve la face opaque visible a travers un bloc de verre.
    return neighbour == glass;
}

[[nodiscard]] auto make_cell_coordinate(
    const FaceDefinition& definition,
    int slice,
    int u,
    int v) noexcept -> BlockCoord {
    BlockCoord result {};
    set_axis_value(result, definition.normal_axis, slice);
    set_axis_value(result, definition.u_axis, u);
    set_axis_value(result, definition.v_axis, v);
    return result;
}

[[nodiscard]] auto offset_axis(BlockCoord coordinate, int axis, int delta) noexcept
    -> BlockCoord {
    set_axis_value(coordinate, axis, axis_value(coordinate, axis) + delta);
    return coordinate;
}

[[nodiscard]] auto edge_is_exposed(
    const SampledVolume& volume,
    const FaceDefinition& definition,
    int slice,
    int u_start,
    int v_start,
    int width,
    int height,
    int edge_axis,
    int edge_sign) noexcept -> bool {
    const auto count = edge_axis == definition.u_axis ? height : width;
    for (int offset = 0; offset < count; ++offset) {
        auto cell = make_cell_coordinate(
            definition,
            slice,
            edge_axis == definition.u_axis
                ? u_start
                : u_start + offset,
            edge_axis == definition.v_axis
                ? v_start
                : v_start + offset);
        cell = offset_axis(cell, edge_axis, edge_sign);
        if (!volume.contains(cell) || is_block_solid(volume.get(cell).block_id)) {
            return false;
        }
    }
    return true;
}

[[nodiscard]] auto bevel_flags(
    const SampledVolume& volume,
    const FaceDefinition& definition,
    int slice,
    int u_start,
    int v_start,
    int width,
    int height,
    bool enabled) noexcept -> std::uint8_t {
    if (!enabled) {
        return 0U;
    }
    std::uint8_t flags = 0U;
    if (edge_is_exposed(
            volume,
            definition,
            slice,
            u_start,
            v_start,
            width,
            height,
            definition.u_axis,
            -1)) {
        flags |= ArchitecturalBevelNegativeU;
    }
    if (edge_is_exposed(
            volume,
            definition,
            slice,
            u_start + width - 1,
            v_start,
            1,
            height,
            definition.u_axis,
            1)) {
        flags |= ArchitecturalBevelPositiveU;
    }
    if (edge_is_exposed(
            volume,
            definition,
            slice,
            u_start,
            v_start,
            width,
            height,
            definition.v_axis,
            -1)) {
        flags |= ArchitecturalBevelNegativeV;
    }
    if (edge_is_exposed(
            volume,
            definition,
            slice,
            u_start,
            v_start + height - 1,
            width,
            1,
            definition.v_axis,
            1)) {
        flags |= ArchitecturalBevelPositiveV;
    }
    return flags;
}

void include_point(ArchitecturalBounds& bounds, float x, float y, float z) noexcept {
    if (!bounds.valid) {
        bounds = {x, y, z, x, y, z, true};
        return;
    }
    bounds.min_x = std::min(bounds.min_x, x);
    bounds.min_y = std::min(bounds.min_y, y);
    bounds.min_z = std::min(bounds.min_z, z);
    bounds.max_x = std::max(bounds.max_x, x);
    bounds.max_y = std::max(bounds.max_y, y);
    bounds.max_z = std::max(bounds.max_z, z);
}

void include_bounds(
    ArchitecturalBounds& destination,
    const ArchitecturalBounds& source) noexcept {
    if (!source.valid) {
        return;
    }
    include_point(destination, source.min_x, source.min_y, source.min_z);
    include_point(destination, source.max_x, source.max_y, source.max_z);
}

[[nodiscard]] auto fixed_uv(int blocks) noexcept -> std::uint16_t {
    return static_cast<std::uint16_t>(
        static_cast<unsigned int>(blocks) *
        static_cast<unsigned int>(kUvUnitsPerBlock));
}

[[nodiscard]] auto vertex_position(
    const FaceDefinition& definition,
    int slice,
    int u,
    int v,
    int plane_offset) noexcept -> std::array<float, 3> {
    std::array<float, 3> position {};
    position[static_cast<std::size_t>(definition.normal_axis)] =
        static_cast<float>(slice + plane_offset);
    position[static_cast<std::size_t>(definition.u_axis)] =
        static_cast<float>(u);
    position[static_cast<std::size_t>(definition.v_axis)] =
        static_cast<float>(v);
    return position;
}

void append_quad(
    ArchitecturalMesh& mesh,
    const SampledVolume& volume,
    const FaceDefinition& definition,
    int slice,
    int u_start,
    int v_start,
    int width,
    int height,
    const FaceMaskCell& mask,
    bool mark_silhouette_bevels) {
    const auto plane_offset = definition.normal_sign > 0 ? 1 : 0;
    const auto p0 = vertex_position(
        definition,
        slice,
        u_start,
        v_start,
        plane_offset);
    const auto p1 = vertex_position(
        definition,
        slice,
        u_start + width,
        v_start,
        plane_offset);
    const auto p2 = vertex_position(
        definition,
        slice,
        u_start + width,
        v_start + height,
        plane_offset);
    const auto p3 = vertex_position(
        definition,
        slice,
        u_start,
        v_start + height,
        plane_offset);

    auto flags = bevel_flags(
        volume,
        definition,
        slice,
        u_start,
        v_start,
        width,
        height,
        mark_silhouette_bevels);
    if (mask.material_block == to_block_id(BlockType::Glass)) {
        flags |= ArchitecturalTransparent;
    }

    const auto first_vertex = static_cast<std::uint32_t>(mesh.vertices.size());
    const auto first_index = static_cast<std::uint32_t>(mesh.indices.size());
    const auto u_max = fixed_uv(width);
    const auto v_max = fixed_uv(height);
    const auto make_vertex = [&](const std::array<float, 3>& position,
                                 std::uint16_t u,
                                 std::uint16_t v) {
        HardSurfaceVertex vertex {};
        vertex.x = position[0];
        vertex.y = position[1];
        vertex.z = position[2];
        vertex.nx = definition.normal[0];
        vertex.ny = definition.normal[1];
        vertex.nz = definition.normal[2];
        vertex.u_fixed = u;
        vertex.v_fixed = v;
        vertex.material_block = mask.material_block;
        vertex.sky_light = mask.sky_light;
        vertex.block_light = mask.block_light;
        vertex.surface_flags = flags;
        return vertex;
    };
    mesh.vertices.push_back(make_vertex(p0, 0U, 0U));
    mesh.vertices.push_back(make_vertex(p1, u_max, 0U));
    mesh.vertices.push_back(make_vertex(p2, u_max, v_max));
    mesh.vertices.push_back(make_vertex(p3, 0U, v_max));
    mesh.indices.insert(
        mesh.indices.end(),
        {
            first_vertex,
            first_vertex + 1U,
            first_vertex + 2U,
            first_vertex,
            first_vertex + 2U,
            first_vertex + 3U,
        });

    mesh.quads.push_back({
        first_vertex,
        first_index,
        static_cast<std::uint16_t>(width),
        static_cast<std::uint16_t>(height),
        make_cell_coordinate(definition, slice, u_start, v_start),
        definition.face,
        mask.material_block,
        flags,
    });
    for (const auto& position : {p0, p1, p2, p3}) {
        include_point(mesh.bounds, position[0], position[1], position[2]);
    }
}

void build_face_direction(
    ArchitecturalMesh& mesh,
    const ArchitecturalSection& section,
    const SampledVolume& volume,
    const FaceDefinition& definition,
    bool mark_silhouette_bevels) {
    const auto normal_min = axis_min(section, definition.normal_axis);
    const auto normal_max = axis_max(section, definition.normal_axis);
    const auto u_min = axis_min(section, definition.u_axis);
    const auto u_max = axis_max(section, definition.u_axis);
    const auto v_min = axis_min(section, definition.v_axis);
    const auto v_max = axis_max(section, definition.v_axis);
    const auto u_size = static_cast<std::size_t>(u_max - u_min + 1);
    const auto v_size = static_cast<std::size_t>(v_max - v_min + 1);
    std::vector<FaceMaskCell> mask(u_size * v_size);
    std::vector<bool> consumed(u_size * v_size, false);

    for (int slice = normal_min; slice <= normal_max; ++slice) {
        std::fill(mask.begin(), mask.end(), FaceMaskCell {});
        std::fill(consumed.begin(), consumed.end(), false);
        for (int v = v_min; v <= v_max; ++v) {
            for (int u = u_min; u <= u_max; ++u) {
                const auto coordinate =
                    make_cell_coordinate(definition, slice, u, v);
                const auto& sample = volume.get(coordinate);
                if (!is_architectural_solid_block(sample.block_id)) {
                    continue;
                }
                const auto neighbour = offset_axis(
                    coordinate,
                    definition.normal_axis,
                    definition.normal_sign);
                if (!face_is_visible(
                        sample.block_id,
                        volume.get(neighbour).block_id)) {
                    continue;
                }
                const auto mask_index =
                    static_cast<std::size_t>(v - v_min) * u_size +
                    static_cast<std::size_t>(u - u_min);
                mask[mask_index] = {
                    true,
                    sample.block_id,
                    sample.sky_light,
                    sample.block_light,
                };
            }
        }

        for (int v = v_min; v <= v_max; ++v) {
            for (int u = u_min; u <= u_max; ++u) {
                const auto local_u = static_cast<std::size_t>(u - u_min);
                const auto local_v = static_cast<std::size_t>(v - v_min);
                const auto start_index = local_v * u_size + local_u;
                if (consumed[start_index] || !mask[start_index].visible) {
                    continue;
                }
                const auto key = mask[start_index];
                int width = 1;
                while (u + width <= u_max) {
                    const auto candidate_index =
                        local_v * u_size +
                        static_cast<std::size_t>(u + width - u_min);
                    if (consumed[candidate_index] ||
                        !(mask[candidate_index] == key)) {
                        break;
                    }
                    ++width;
                }

                int height = 1;
                bool can_grow = true;
                while (v + height <= v_max && can_grow) {
                    const auto candidate_v =
                        static_cast<std::size_t>(v + height - v_min);
                    for (int width_offset = 0;
                         width_offset < width;
                         ++width_offset) {
                        const auto candidate_index =
                            candidate_v * u_size +
                            static_cast<std::size_t>(
                                u + width_offset - u_min);
                        if (consumed[candidate_index] ||
                            !(mask[candidate_index] == key)) {
                            can_grow = false;
                            break;
                        }
                    }
                    if (can_grow) {
                        ++height;
                    }
                }

                for (int height_offset = 0;
                     height_offset < height;
                     ++height_offset) {
                    const auto consumed_v =
                        static_cast<std::size_t>(
                            v + height_offset - v_min);
                    for (int width_offset = 0;
                         width_offset < width;
                         ++width_offset) {
                        const auto consumed_index =
                            consumed_v * u_size +
                            static_cast<std::size_t>(
                                u + width_offset - u_min);
                        consumed[consumed_index] = true;
                    }
                }
                append_quad(
                    mesh,
                    volume,
                    definition,
                    slice,
                    u,
                    v,
                    width,
                    height,
                    key,
                    mark_silhouette_bevels);
            }
        }
    }
}

[[nodiscard]] auto fixture_kind(BlockId block) noexcept
    -> ArchitecturalFixtureKind {
    switch (static_cast<BlockType>(block)) {
    case BlockType::TorchWallPositiveX:
        return ArchitecturalFixtureKind::WallTorchPositiveX;
    case BlockType::TorchWallNegativeX:
        return ArchitecturalFixtureKind::WallTorchNegativeX;
    case BlockType::TorchWallPositiveZ:
        return ArchitecturalFixtureKind::WallTorchPositiveZ;
    case BlockType::TorchWallNegativeZ:
        return ArchitecturalFixtureKind::WallTorchNegativeZ;
    case BlockType::Torch:
    default:
        return ArchitecturalFixtureKind::FloorTorch;
    }
}

void append_fixture(
    ArchitecturalMesh& mesh,
    BlockCoord coordinate,
    const ArchitecturalCellSample& sample) {
    const auto support = torch_support_offset(sample.block_id);
    ArchitecturalFixtureInstance fixture {};
    fixture.position_x =
        static_cast<float>(coordinate.x) + 0.5F +
        static_cast<float>(support.x) * 0.28125F;
    fixture.position_y =
        static_cast<float>(coordinate.y) +
        (is_wall_torch_block(sample.block_id) ? 0.35F : 0.44F);
    fixture.position_z =
        static_cast<float>(coordinate.z) + 0.5F +
        static_cast<float>(support.z) * 0.28125F;
    if (is_wall_torch_block(sample.block_id)) {
        constexpr float kWallHorizontal = 0.3826834324F;
        constexpr float kWallVertical = 0.9238795325F;
        fixture.direction_x =
            -static_cast<float>(support.x) * kWallHorizontal;
        fixture.direction_y = kWallVertical;
        fixture.direction_z =
            -static_cast<float>(support.z) * kWallHorizontal;
    }
    fixture.owner_cell = coordinate;
    fixture.bounds = {
        static_cast<float>(coordinate.x),
        static_cast<float>(coordinate.y),
        static_cast<float>(coordinate.z),
        static_cast<float>(coordinate.x + 1),
        static_cast<float>(coordinate.y + 1),
        static_cast<float>(coordinate.z + 1),
        true,
    };
    fixture.kind = fixture_kind(sample.block_id);
    fixture.source_block = sample.block_id;
    fixture.sky_light = sample.sky_light;
    fixture.block_light = std::max<std::uint8_t>(sample.block_light, 14U);
    mesh.fixtures.push_back(fixture);
    include_bounds(mesh.bounds, fixture.bounds);
}

void append_fixtures(
    ArchitecturalMesh& mesh,
    const ArchitecturalSection& section,
    const SampledVolume& volume) {
    for (int y = section.min.y; y <= section.max.y; ++y) {
        for (int z = section.min.z; z <= section.max.z; ++z) {
            for (int x = section.min.x; x <= section.max.x; ++x) {
                const BlockCoord coordinate {x, y, z};
                const auto& sample = volume.get(coordinate);
                if (is_architectural_fixture_block(sample.block_id)) {
                    append_fixture(mesh, coordinate, sample);
                }
            }
        }
    }
}

void hash_byte(std::uint64_t& hash, std::uint8_t value) noexcept {
    hash ^= static_cast<std::uint64_t>(value);
    hash *= kFnvPrime;
}

void hash_u16(std::uint64_t& hash, std::uint16_t value) noexcept {
    hash_byte(hash, static_cast<std::uint8_t>(value & 0xFFU));
    hash_byte(hash, static_cast<std::uint8_t>((value >> 8U) & 0xFFU));
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

void hash_bounds(std::uint64_t& hash, const ArchitecturalBounds& bounds) noexcept {
    hash_float(hash, bounds.min_x);
    hash_float(hash, bounds.min_y);
    hash_float(hash, bounds.min_z);
    hash_float(hash, bounds.max_x);
    hash_float(hash, bounds.max_y);
    hash_float(hash, bounds.max_z);
    hash_byte(hash, bounds.valid ? 1U : 0U);
}

} // namespace

auto ArchitecturalSection::valid() const noexcept -> bool {
    return min.x <= max.x && min.y <= max.y && min.z <= max.z;
}

auto ArchitecturalSection::contains(BlockCoord coordinate) const noexcept -> bool {
    return coordinate.x >= min.x && coordinate.x <= max.x &&
           coordinate.y >= min.y && coordinate.y <= max.y &&
           coordinate.z >= min.z && coordinate.z <= max.z;
}

ArchitecturalMesher::ArchitecturalMesher(
    ArchitecturalMesherSettings settings) noexcept
    : settings_(settings) {}

auto ArchitecturalMesher::build_mesh(
    const ArchitecturalSection& section,
    const ArchitecturalSampler& sampler,
    std::size_t vertex_reserve_hint,
    std::size_t index_reserve_hint) const -> ArchitecturalMesh {
    const auto volume = sample_volume(section, sampler);
    ArchitecturalMesh mesh {};
    mesh.vertices.reserve(vertex_reserve_hint);
    mesh.indices.reserve(index_reserve_hint);
    for (const auto& definition : kFaceDefinitions) {
        build_face_direction(
            mesh,
            section,
            volume,
            definition,
            settings_.mark_silhouette_bevels);
    }
    append_fixtures(mesh, section, volume);
    return mesh;
}

auto ArchitecturalMesher::settings() const noexcept
    -> const ArchitecturalMesherSettings& {
    return settings_;
}

auto architectural_mesh_deterministic_hash(
    const ArchitecturalMesh& mesh) noexcept -> std::uint64_t {
    auto hash = kFnvOffset;
    hash_u64(hash, static_cast<std::uint64_t>(mesh.vertices.size()));
    for (const auto& vertex : mesh.vertices) {
        hash_float(hash, vertex.x);
        hash_float(hash, vertex.y);
        hash_float(hash, vertex.z);
        hash_float(hash, vertex.nx);
        hash_float(hash, vertex.ny);
        hash_float(hash, vertex.nz);
        hash_u16(hash, vertex.u_fixed);
        hash_u16(hash, vertex.v_fixed);
        hash_byte(hash, vertex.material_block);
        hash_byte(hash, vertex.sky_light);
        hash_byte(hash, vertex.block_light);
        hash_byte(hash, vertex.surface_flags);
    }
    hash_u64(hash, static_cast<std::uint64_t>(mesh.indices.size()));
    for (const auto index : mesh.indices) {
        hash_u32(hash, index);
    }
    hash_u64(hash, static_cast<std::uint64_t>(mesh.quads.size()));
    for (const auto& quad : mesh.quads) {
        hash_u32(hash, quad.first_vertex);
        hash_u32(hash, quad.first_index);
        hash_u16(hash, quad.width);
        hash_u16(hash, quad.height);
        hash_coordinate(hash, quad.owner_cell);
        hash_byte(hash, static_cast<std::uint8_t>(quad.face));
        hash_byte(hash, quad.material_block);
        hash_byte(hash, quad.surface_flags);
    }
    hash_u64(hash, static_cast<std::uint64_t>(mesh.fixtures.size()));
    for (const auto& fixture : mesh.fixtures) {
        hash_float(hash, fixture.position_x);
        hash_float(hash, fixture.position_y);
        hash_float(hash, fixture.position_z);
        hash_float(hash, fixture.direction_x);
        hash_float(hash, fixture.direction_y);
        hash_float(hash, fixture.direction_z);
        hash_coordinate(hash, fixture.owner_cell);
        hash_bounds(hash, fixture.bounds);
        hash_byte(hash, static_cast<std::uint8_t>(fixture.kind));
        hash_byte(hash, fixture.source_block);
        hash_byte(hash, fixture.sky_light);
        hash_byte(hash, fixture.block_light);
    }
    hash_bounds(hash, mesh.bounds);
    return hash;
}

} // namespace valcraft
