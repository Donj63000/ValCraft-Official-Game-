#include "render/ArchitecturalFixtureMesh.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <stdexcept>
#include <vector>

namespace valcraft {

namespace {

constexpr float kNormalEpsilonSquared = 1.0e-12F;
constexpr float kTriangleEpsilonSquared = 1.0e-14F;
constexpr double kCellContainmentTolerance = 1.0e-5;
constexpr float kShaftRadiusScale = 0.18F;
constexpr float kFloorShaftLength = 0.62F;
constexpr float kFloorShaftCenterOffset = -0.13F;
constexpr float kWallShaftLength = 0.54F;
constexpr float kWallShaftCenterOffset = -0.07F;
constexpr float kFlameRadialScale = 0.24F;
constexpr float kFlameAxialScale = 0.32F;
constexpr float kFlameCenterOffset = 0.30F;
constexpr float kFixtureUvUnits = 256.0F;
// Je dimensionne le preflight sur le profil Medium, qui est aussi le plafond
// utilise lorsque l'appelant demande High : 192 sommets de hampe et 864 de
// flamme, avec le meme nombre d'indices.
constexpr std::size_t kMaximumFixtureVerticesPerInstance = 1'056U;
constexpr std::size_t kMaximumFixtureIndicesPerInstance = 1'056U;

struct FixtureVector {
    float x = 0.0F;
    float y = 0.0F;
    float z = 0.0F;
};

struct FixtureBasis {
    FixtureVector right {1.0F, 0.0F, 0.0F};
    FixtureVector up {0.0F, 1.0F, 0.0F};
    FixtureVector forward {0.0F, 0.0F, 1.0F};
};

struct FixtureGeometry {
    std::vector<HardSurfaceVertex> vertices;
    std::vector<std::uint32_t> indices;
    ArchitecturalBounds bounds {};
};

[[nodiscard]] constexpr auto add(
    const FixtureVector& lhs,
    const FixtureVector& rhs) noexcept -> FixtureVector {

    return {
        lhs.x + rhs.x,
        lhs.y + rhs.y,
        lhs.z + rhs.z,
    };
}

[[nodiscard]] constexpr auto subtract(
    const FixtureVector& lhs,
    const FixtureVector& rhs) noexcept -> FixtureVector {

    return {
        lhs.x - rhs.x,
        lhs.y - rhs.y,
        lhs.z - rhs.z,
    };
}

[[nodiscard]] constexpr auto multiply(
    const FixtureVector& value,
    float scale) noexcept -> FixtureVector {

    return {
        value.x * scale,
        value.y * scale,
        value.z * scale,
    };
}

[[nodiscard]] constexpr auto dot(
    const FixtureVector& lhs,
    const FixtureVector& rhs) noexcept -> float {

    return
        lhs.x * rhs.x +
        lhs.y * rhs.y +
        lhs.z * rhs.z;
}

[[nodiscard]] constexpr auto cross(
    const FixtureVector& lhs,
    const FixtureVector& rhs) noexcept -> FixtureVector {

    return {
        lhs.y * rhs.z - lhs.z * rhs.y,
        lhs.z * rhs.x - lhs.x * rhs.z,
        lhs.x * rhs.y - lhs.y * rhs.x,
    };
}

[[nodiscard]] auto is_finite(
    const FixtureVector& value) noexcept -> bool {

    return
        std::isfinite(value.x) &&
        std::isfinite(value.y) &&
        std::isfinite(value.z);
}

[[nodiscard]] auto normalized(
    const FixtureVector& value) -> FixtureVector {

    const auto length_squared = dot(value, value);
    if (!std::isfinite(length_squared) ||
        length_squared <= kNormalEpsilonSquared) {
        throw std::invalid_argument(
            "La direction de fixture architecturale est invalide");
    }
    return multiply(
        value,
        1.0F / std::sqrt(length_squared));
}

[[nodiscard]] auto fixture_lod(
    StylizedPrimitiveLod lod) noexcept -> StylizedPrimitiveLod {

    switch (lod) {
    case StylizedPrimitiveLod::Low:
        return StylizedPrimitiveLod::Low;
    case StylizedPrimitiveLod::Medium:
    case StylizedPrimitiveLod::High:
    default:
        // Je borne les torches a deux LOD : le profil High reutilise le
        // maillage Medium afin de ne pas augmenter leur cout a courte portee.
        return StylizedPrimitiveLod::Medium;
    }
}

[[nodiscard]] auto basis_for(
    const ArchitecturalFixtureInstance& fixture) -> FixtureBasis {

    const FixtureVector source_direction {
        fixture.direction_x,
        fixture.direction_y,
        fixture.direction_z,
    };
    const auto up = normalized(source_direction);
    const FixtureVector reference =
        std::abs(up.z) < 0.99F
            ? FixtureVector {0.0F, 0.0F, 1.0F}
            : FixtureVector {1.0F, 0.0F, 0.0F};
    const auto right = normalized(cross(up, reference));
    const auto forward = normalized(cross(right, up));
    return {right, up, forward};
}

[[nodiscard]] constexpr auto transform_vector(
    const FixtureBasis& basis,
    const FixtureVector& local) noexcept -> FixtureVector {

    return add(
        add(
            multiply(basis.right, local.x),
            multiply(basis.up, local.y)),
        multiply(basis.forward, local.z));
}

[[nodiscard]] auto fixed_uv(float value) noexcept -> std::uint16_t {
    const auto scaled =
        std::round(
            std::clamp(value, 0.0F, 1.0F) *
            kFixtureUvUnits);
    return static_cast<std::uint16_t>(scaled);
}

void include_point(
    ArchitecturalBounds& bounds,
    const FixtureVector& point) noexcept {

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

void include_bounds(
    ArchitecturalBounds& destination,
    const ArchitecturalBounds& source) noexcept {

    if (!source.valid) {
        return;
    }
    include_point(
        destination,
        {source.min_x, source.min_y, source.min_z});
    include_point(
        destination,
        {source.max_x, source.max_y, source.max_z});
}

[[nodiscard]] auto point_is_inside_owner_cell(
    const FixtureVector& point,
    BlockCoord owner_cell) noexcept -> bool {

    const auto min_x = static_cast<double>(owner_cell.x);
    const auto min_y = static_cast<double>(owner_cell.y);
    const auto min_z = static_cast<double>(owner_cell.z);
    const auto max_x = min_x + 1.0;
    const auto max_y = min_y + 1.0;
    const auto max_z = min_z + 1.0;
    return
        static_cast<double>(point.x) >=
            min_x - kCellContainmentTolerance &&
        static_cast<double>(point.x) <=
            max_x + kCellContainmentTolerance &&
        static_cast<double>(point.y) >=
            min_y - kCellContainmentTolerance &&
        static_cast<double>(point.y) <=
            max_y + kCellContainmentTolerance &&
        static_cast<double>(point.z) >=
            min_z - kCellContainmentTolerance &&
        static_cast<double>(point.z) <=
            max_z + kCellContainmentTolerance;
}

void validate_triangle(
    const FixtureGeometry& geometry,
    std::size_t index_offset) {

    const auto index_a = geometry.indices[index_offset];
    const auto index_b = geometry.indices[index_offset + 1U];
    const auto index_c = geometry.indices[index_offset + 2U];
    if (index_a >= geometry.vertices.size() ||
        index_b >= geometry.vertices.size() ||
        index_c >= geometry.vertices.size()) {
        throw std::logic_error(
            "La fixture architecturale contient un index invalide");
    }

    const auto& a = geometry.vertices[index_a];
    const auto& b = geometry.vertices[index_b];
    const auto& c = geometry.vertices[index_c];
    const FixtureVector point_a {a.x, a.y, a.z};
    const FixtureVector point_b {b.x, b.y, b.z};
    const FixtureVector point_c {c.x, c.y, c.z};
    const auto geometric_normal = cross(
        subtract(point_b, point_a),
        subtract(point_c, point_a));
    const auto area_squared =
        dot(geometric_normal, geometric_normal);
    const FixtureVector stored_normal {
        a.nx + b.nx + c.nx,
        a.ny + b.ny + c.ny,
        a.nz + b.nz + c.nz,
    };
    if (!std::isfinite(area_squared) ||
        area_squared <= kTriangleEpsilonSquared ||
        dot(geometric_normal, stored_normal) <= 0.0F) {
        throw std::logic_error(
            "La fixture architecturale contient un triangle degenere");
    }
}

void append_primitive(
    FixtureGeometry& destination,
    const StylizedPrimitiveMesh& primitive,
    const ArchitecturalFixtureInstance& fixture,
    const FixtureBasis& basis,
    const FixtureVector& center,
    const FixtureVector& scale,
    BlockId material_block) {

    if (primitive.empty() ||
        primitive.indices.size() % 3U != 0U) {
        throw std::logic_error(
            "La primitive de fixture architecturale est invalide");
    }
    const auto first_vertex = destination.vertices.size();
    if (first_vertex >
        static_cast<std::size_t>(
            std::numeric_limits<std::uint32_t>::max()) -
            primitive.vertices.size()) {
        throw std::length_error(
            "La fixture architecturale depasse la plage de ses indices");
    }

    const auto sky_light =
        std::min<std::uint8_t>(fixture.sky_light, 15U);
    const auto block_light =
        std::min<std::uint8_t>(fixture.block_light, 15U);
    destination.vertices.reserve(
        destination.vertices.size() +
        primitive.vertices.size());
    for (const auto& source : primitive.vertices) {
        const FixtureVector scaled_position {
            source.x * scale.x,
            source.y * scale.y,
            source.z * scale.z,
        };
        const auto position = add(
            center,
            transform_vector(
                basis,
                scaled_position));
        if (!is_finite(position) ||
            !point_is_inside_owner_cell(
                position,
                fixture.owner_cell)) {
            throw std::invalid_argument(
                "La geometrie de fixture sort de sa cellule logique");
        }

        const FixtureVector inverse_scaled_normal {
            source.nx / scale.x,
            source.ny / scale.y,
            source.nz / scale.z,
        };
        const auto normal = normalized(
            transform_vector(
                basis,
                inverse_scaled_normal));

        HardSurfaceVertex vertex {};
        vertex.x = position.x;
        vertex.y = position.y;
        vertex.z = position.z;
        vertex.nx = normal.x;
        vertex.ny = normal.y;
        vertex.nz = normal.z;
        vertex.u_fixed = fixed_uv(source.u);
        vertex.v_fixed = fixed_uv(source.v);
        vertex.material_block = material_block;
        vertex.sky_light = sky_light;
        vertex.block_light = block_light;
        vertex.surface_flags = 0U;
        destination.vertices.push_back(vertex);
        include_point(destination.bounds, position);
    }

    const auto first_index = destination.indices.size();
    destination.indices.reserve(
        destination.indices.size() +
        primitive.indices.size());
    for (const auto source_index : primitive.indices) {
        if (source_index >= primitive.vertices.size()) {
            throw std::logic_error(
                "La primitive de fixture contient un index invalide");
        }
        destination.indices.push_back(
            static_cast<std::uint32_t>(first_vertex) +
            source_index);
    }
    for (auto index_offset = first_index;
         index_offset < destination.indices.size();
         index_offset += 3U) {
        validate_triangle(destination, index_offset);
    }
}

void append_fixture(
    FixtureGeometry& destination,
    const ArchitecturalFixtureInstance& fixture,
    const StylizedPrimitiveMesh& shaft,
    const StylizedPrimitiveMesh& flame) {

    if (!is_torch_block(fixture.source_block)) {
        throw std::invalid_argument(
            "La fixture architecturale ne reference pas une torche");
    }
    const FixtureVector fixture_position {
        fixture.position_x,
        fixture.position_y,
        fixture.position_z,
    };
    if (!is_finite(fixture_position)) {
        throw std::invalid_argument(
            "La position de fixture architecturale est invalide");
    }

    const auto basis = basis_for(fixture);
    const auto wall_torch =
        is_wall_torch_block(fixture.source_block);
    const auto shaft_center_offset =
        wall_torch
            ? kWallShaftCenterOffset
            : kFloorShaftCenterOffset;
    const auto shaft_length =
        wall_torch
            ? kWallShaftLength
            : kFloorShaftLength;
    const auto shaft_center = add(
        fixture_position,
        multiply(
            basis.up,
            shaft_center_offset));
    const auto flame_center = add(
        fixture_position,
        multiply(
            basis.up,
            kFlameCenterOffset));

    append_primitive(
        destination,
        shaft,
        fixture,
        basis,
        shaft_center,
        {
            kShaftRadiusScale,
            shaft_length,
            kShaftRadiusScale,
        },
        to_block_id(BlockType::Wood));
    append_primitive(
        destination,
        flame,
        fixture,
        basis,
        flame_center,
        {
            kFlameRadialScale,
            kFlameAxialScale,
            kFlameRadialScale,
        },
        fixture.source_block);
}

} // namespace

auto append_architectural_fixture_geometry(
    ArchitecturalMesh& mesh,
    StylizedPrimitiveLod lod) -> std::size_t {

    const auto first_added_index = mesh.indices.size();
    if (mesh.fixtures.size() > kMaximumArchitecturalFixtures) {
        throw std::length_error(
            "Le maillage contient trop de fixtures architecturales");
    }
    // Je refuse un prefixe ou un ajout excessif avant de construire les deux
    // primitives et leurs buffers temporaires. Le mesh appelant reste donc
    // strictement intact sur ce chemin de rejet.
    const auto maximum_size = checked_architectural_mesh_growth(
        mesh.vertices.size(),
        mesh.indices.size(),
        mesh.fixtures.size() *
            kMaximumFixtureVerticesPerInstance,
        mesh.fixtures.size() *
            kMaximumFixtureIndicesPerInstance);
    if (maximum_size.vertex_count > mesh.vertices.max_size() ||
        maximum_size.index_count > mesh.indices.max_size()) {
        throw std::length_error(
            "Le maillage des fixtures architecturales est trop grand");
    }
    if (mesh.fixtures.empty()) {
        return first_added_index;
    }

    const auto resolved_lod = fixture_lod(lod);
    const auto shaft =
        build_stylized_tapered_cylinder(resolved_lod);
    const auto flame =
        build_stylized_ellipsoid(resolved_lod);
    const auto vertices_per_fixture =
        shaft.vertices.size() +
        flame.vertices.size();
    const auto indices_per_fixture =
        shaft.indices.size() +
        flame.indices.size();
    if (vertices_per_fixture == 0U ||
        indices_per_fixture == 0U ||
        vertices_per_fixture >
            kMaximumFixtureVerticesPerInstance ||
        indices_per_fixture >
            kMaximumFixtureIndicesPerInstance) {
        throw std::logic_error(
            "La primitive de fixture depasse son budget contractuel");
    }
    const auto final_size = checked_architectural_mesh_growth(
        mesh.vertices.size(),
        mesh.indices.size(),
        mesh.fixtures.size() * vertices_per_fixture,
        mesh.fixtures.size() * indices_per_fixture);

    FixtureGeometry addition {};
    addition.vertices.reserve(
        mesh.fixtures.size() *
        vertices_per_fixture);
    addition.indices.reserve(
        mesh.fixtures.size() *
        indices_per_fixture);
    for (const auto& fixture : mesh.fixtures) {
        append_fixture(
            addition,
            fixture,
            shaft,
            flame);
    }

    const auto base_vertex =
        static_cast<std::uint32_t>(mesh.vertices.size());
    mesh.vertices.reserve(final_size.vertex_count);
    mesh.indices.reserve(final_size.index_count);
    mesh.vertices.insert(
        mesh.vertices.end(),
        addition.vertices.begin(),
        addition.vertices.end());
    for (const auto local_index : addition.indices) {
        mesh.indices.push_back(
            base_vertex +
            local_index);
    }
    include_bounds(mesh.bounds, addition.bounds);
    return first_added_index;
}

} // namespace valcraft
