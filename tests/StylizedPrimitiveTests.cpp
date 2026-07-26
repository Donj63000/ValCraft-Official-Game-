#include "render/StylizedPrimitives.h"

#include <doctest/doctest.h>
#include <glm/common.hpp>
#include <glm/geometric.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstring>
#include <limits>

namespace valcraft {

namespace {

constexpr std::array<StylizedPrimitiveType, 6> kPrimitiveTypes {{
    StylizedPrimitiveType::RoundedBox,
    StylizedPrimitiveType::Capsule,
    StylizedPrimitiveType::Ellipsoid,
    StylizedPrimitiveType::TaperedCylinder,
    StylizedPrimitiveType::Panel,
    StylizedPrimitiveType::Ribbon,
}};

constexpr std::array<StylizedPrimitiveLod, 3> kPrimitiveLods {{
    StylizedPrimitiveLod::Low,
    StylizedPrimitiveLod::Medium,
    StylizedPrimitiveLod::High,
}};

[[nodiscard]] auto position_of(const StylizedPrimitiveVertex& vertex) noexcept -> glm::vec3 {
    return glm::vec3 {vertex.x, vertex.y, vertex.z};
}

[[nodiscard]] auto normal_of(const StylizedPrimitiveVertex& vertex) noexcept -> glm::vec3 {
    return glm::vec3 {vertex.nx, vertex.ny, vertex.nz};
}

[[nodiscard]] auto exact_mesh_match(
    const StylizedPrimitiveMesh& lhs,
    const StylizedPrimitiveMesh& rhs) -> bool {
    if (lhs.vertices.size() != rhs.vertices.size() ||
        lhs.indices.size() != rhs.indices.size()) {
        return false;
    }
    if (!lhs.vertices.empty() &&
        std::memcmp(
            lhs.vertices.data(),
            rhs.vertices.data(),
            lhs.vertices.size() * sizeof(StylizedPrimitiveVertex)) != 0) {
        return false;
    }
    if (!lhs.indices.empty() &&
        std::memcmp(
            lhs.indices.data(),
            rhs.indices.data(),
            lhs.indices.size() * sizeof(std::uint32_t)) != 0) {
        return false;
    }
    return lhs.bounds.min.x == rhs.bounds.min.x &&
        lhs.bounds.min.y == rhs.bounds.min.y &&
        lhs.bounds.min.z == rhs.bounds.min.z &&
        lhs.bounds.max.x == rhs.bounds.max.x &&
        lhs.bounds.max.y == rhs.bounds.max.y &&
        lhs.bounds.max.z == rhs.bounds.max.z;
}

void check_expected_bounds(
    const StylizedPrimitiveMesh& mesh,
    const glm::vec3& expected_minimum,
    const glm::vec3& expected_maximum) {
    CHECK(mesh.bounds.min.x == doctest::Approx(expected_minimum.x).epsilon(0.000001));
    CHECK(mesh.bounds.min.y == doctest::Approx(expected_minimum.y).epsilon(0.000001));
    CHECK(mesh.bounds.min.z == doctest::Approx(expected_minimum.z).epsilon(0.000001));
    CHECK(mesh.bounds.max.x == doctest::Approx(expected_maximum.x).epsilon(0.000001));
    CHECK(mesh.bounds.max.y == doctest::Approx(expected_maximum.y).epsilon(0.000001));
    CHECK(mesh.bounds.max.z == doctest::Approx(expected_maximum.z).epsilon(0.000001));
}

void check_mesh_integrity(const StylizedPrimitiveMesh& mesh) {
    REQUIRE_FALSE(mesh.empty());
    REQUIRE(mesh.indices.size() % 3U == 0U);
    CHECK(mesh.triangle_count() == mesh.indices.size() / 3U);

    auto measured_minimum = glm::vec3 {std::numeric_limits<float>::max()};
    auto measured_maximum = glm::vec3 {std::numeric_limits<float>::lowest()};
    for (const auto& vertex : mesh.vertices) {
        const auto position = position_of(vertex);
        const auto normal = normal_of(vertex);
        CHECK(std::isfinite(position.x));
        CHECK(std::isfinite(position.y));
        CHECK(std::isfinite(position.z));
        CHECK(std::isfinite(normal.x));
        CHECK(std::isfinite(normal.y));
        CHECK(std::isfinite(normal.z));
        CHECK(std::isfinite(vertex.u));
        CHECK(std::isfinite(vertex.v));
        CHECK(std::isfinite(vertex.face_index));
        CHECK(glm::dot(normal, normal) == doctest::Approx(1.0F).epsilon(0.00001));
        CHECK(vertex.u >= 0.0F);
        CHECK(vertex.u <= 1.0F);
        CHECK(vertex.v >= 0.0F);
        CHECK(vertex.v <= 1.0F);
        CHECK(vertex.face_index >= 0.0F);
        CHECK(vertex.face_index <= 5.0F);
        CHECK(vertex.face_index == std::floor(vertex.face_index));
        CHECK(position.x >= -0.500001F);
        CHECK(position.x <= 0.500001F);
        CHECK(position.y >= -0.500001F);
        CHECK(position.y <= 0.500001F);
        CHECK(position.z >= -0.500001F);
        CHECK(position.z <= 0.500001F);
        measured_minimum = glm::min(measured_minimum, position);
        measured_maximum = glm::max(measured_maximum, position);
    }

    CHECK(mesh.bounds.min.x == measured_minimum.x);
    CHECK(mesh.bounds.min.y == measured_minimum.y);
    CHECK(mesh.bounds.min.z == measured_minimum.z);
    CHECK(mesh.bounds.max.x == measured_maximum.x);
    CHECK(mesh.bounds.max.y == measured_maximum.y);
    CHECK(mesh.bounds.max.z == measured_maximum.z);

    for (std::size_t triangle = 0; triangle < mesh.indices.size(); triangle += 3U) {
        const auto index_a = mesh.indices[triangle + 0U];
        const auto index_b = mesh.indices[triangle + 1U];
        const auto index_c = mesh.indices[triangle + 2U];
        REQUIRE(index_a < mesh.vertices.size());
        REQUIRE(index_b < mesh.vertices.size());
        REQUIRE(index_c < mesh.vertices.size());

        const auto& vertex_a = mesh.vertices[index_a];
        const auto& vertex_b = mesh.vertices[index_b];
        const auto& vertex_c = mesh.vertices[index_c];
        const auto edge_ab = position_of(vertex_b) - position_of(vertex_a);
        const auto edge_ac = position_of(vertex_c) - position_of(vertex_a);
        const auto cross_product = glm::cross(edge_ab, edge_ac);
        const auto area_squared = glm::dot(cross_product, cross_product);
        CHECK(std::isfinite(area_squared));
        CHECK(area_squared > 1.0e-14F);

        const auto averaged_normal =
            normal_of(vertex_a) + normal_of(vertex_b) + normal_of(vertex_c);
        CHECK(glm::dot(cross_product, averaged_normal) > 0.0F);

        // Je force une face unique par triangle pour ne jamais interpoler
        // entre deux tuiles distinctes de l'atlas historique.
        CHECK(vertex_a.face_index == vertex_b.face_index);
        CHECK(vertex_a.face_index == vertex_c.face_index);
    }
}

} // namespace

TEST_CASE("les sommets stylises restent directement compatibles avec le VAO instancie") {
    CHECK(sizeof(StylizedPrimitiveVertex) == sizeof(float) * 9U);
    CHECK(offsetof(StylizedPrimitiveVertex, x) == 0U);
    CHECK(offsetof(StylizedPrimitiveVertex, nx) == sizeof(float) * 3U);
    CHECK(offsetof(StylizedPrimitiveVertex, u) == sizeof(float) * 6U);
    CHECK(offsetof(StylizedPrimitiveVertex, face_index) == sizeof(float) * 8U);
}

TEST_CASE("chaque primitive et chaque LOD produit un maillage fini valide et coherent") {
    for (const auto type : kPrimitiveTypes) {
        for (const auto lod : kPrimitiveLods) {
            CAPTURE(static_cast<int>(type));
            CAPTURE(static_cast<int>(lod));
            check_mesh_integrity(build_stylized_primitive(type, lod));
        }
    }
}

TEST_CASE("la generation des primitives stylisees est deterministe bit a bit") {
    for (const auto type : kPrimitiveTypes) {
        for (const auto lod : kPrimitiveLods) {
            CAPTURE(static_cast<int>(type));
            CAPTURE(static_cast<int>(lod));
            const auto first = build_stylized_primitive(type, lod);
            const auto second = build_stylized_primitive(type, lod);
            CHECK(exact_mesh_match(first, second));
        }
    }
}

TEST_CASE("les trois LOD augmentent strictement le detail sans changer les bornes") {
    for (const auto type : kPrimitiveTypes) {
        CAPTURE(static_cast<int>(type));
        const auto low = build_stylized_primitive(type, StylizedPrimitiveLod::Low);
        const auto medium = build_stylized_primitive(type, StylizedPrimitiveLod::Medium);
        const auto high = build_stylized_primitive(type, StylizedPrimitiveLod::High);
        CHECK(low.triangle_count() < medium.triangle_count());
        CHECK(medium.triangle_count() < high.triangle_count());
        CHECK(low.bounds.min.x == doctest::Approx(medium.bounds.min.x));
        CHECK(low.bounds.min.y == doctest::Approx(medium.bounds.min.y));
        CHECK(low.bounds.min.z == doctest::Approx(medium.bounds.min.z));
        CHECK(low.bounds.max.x == doctest::Approx(medium.bounds.max.x));
        CHECK(low.bounds.max.y == doctest::Approx(medium.bounds.max.y));
        CHECK(low.bounds.max.z == doctest::Approx(medium.bounds.max.z));
        CHECK(high.bounds.min.x == doctest::Approx(medium.bounds.min.x));
        CHECK(high.bounds.min.y == doctest::Approx(medium.bounds.min.y));
        CHECK(high.bounds.min.z == doctest::Approx(medium.bounds.min.z));
        CHECK(high.bounds.max.x == doctest::Approx(medium.bounds.max.x));
        CHECK(high.bounds.max.y == doctest::Approx(medium.bounds.max.y));
        CHECK(high.bounds.max.z == doctest::Approx(medium.bounds.max.z));
    }
}

TEST_CASE("les gabarits gardent leurs bornes canoniques documentees") {
    for (const auto lod : kPrimitiveLods) {
        check_expected_bounds(
            build_stylized_rounded_box(lod),
            glm::vec3 {-0.5F},
            glm::vec3 {0.5F});
        check_expected_bounds(
            build_stylized_ellipsoid(lod),
            glm::vec3 {-0.5F},
            glm::vec3 {0.5F});
        check_expected_bounds(
            build_stylized_capsule(lod),
            glm::vec3 {-kStylizedCapsuleRadius, -0.5F, -kStylizedCapsuleRadius},
            glm::vec3 {kStylizedCapsuleRadius, 0.5F, kStylizedCapsuleRadius});
        check_expected_bounds(
            build_stylized_tapered_cylinder(lod),
            glm::vec3 {-0.5F},
            glm::vec3 {0.5F});
        check_expected_bounds(
            build_stylized_panel(lod),
            glm::vec3 {-0.5F, -0.5F, 0.0F},
            glm::vec3 {0.5F, 0.5F, kStylizedPanelBow});
        check_expected_bounds(
            build_stylized_ribbon(lod),
            glm::vec3 {-0.5F, -kStylizedRibbonHalfWidth, -kStylizedRibbonWaveAmplitude},
            glm::vec3 {0.5F, kStylizedRibbonHalfWidth, kStylizedRibbonWaveAmplitude});
    }
}

TEST_CASE("la rounded box conserve les six orientations UV de l'atlas de cubes") {
    const auto mesh = build_stylized_rounded_box(StylizedPrimitiveLod::Low);
    std::array<bool, 6> face_seen {};
    for (const auto& vertex : mesh.vertices) {
        const auto face = static_cast<std::size_t>(vertex.face_index);
        REQUIRE(face < face_seen.size());
        face_seen[face] = true;
    }
    CHECK(std::all_of(face_seen.begin(), face_seen.end(), [](bool value) {
        return value;
    }));

    CHECK(std::any_of(mesh.vertices.begin(), mesh.vertices.end(), [](const StylizedPrimitiveVertex& vertex) {
        const auto normal = glm::abs(normal_of(vertex));
        return normal.x > 0.2F && normal.y > 0.2F && normal.z > 0.2F;
    }));
}

TEST_CASE("les valeurs enumerees hostiles ont un repli stable et exploitable") {
    const auto invalid_lod = static_cast<StylizedPrimitiveLod>(255U);
    const auto invalid_type = static_cast<StylizedPrimitiveType>(255U);
    const auto fallback_lod = build_stylized_rounded_box(invalid_lod);
    const auto medium = build_stylized_rounded_box(StylizedPrimitiveLod::Medium);
    CHECK(exact_mesh_match(fallback_lod, medium));

    const auto fallback_type = build_stylized_primitive(invalid_type, invalid_lod);
    CHECK(exact_mesh_match(fallback_type, medium));
}

} // namespace valcraft
