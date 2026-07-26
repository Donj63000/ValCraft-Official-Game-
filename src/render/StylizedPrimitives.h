#pragma once

#include <glm/vec3.hpp>

#include <cstddef>
#include <cstdint>
#include <vector>

namespace valcraft {

// Je garde exactement l'organisation du sommet de boite instancie historique :
// position, normale, UV local puis index de la face d'atlas.
struct StylizedPrimitiveVertex {
    float x = 0.0F;
    float y = 0.0F;
    float z = 0.0F;
    float nx = 0.0F;
    float ny = 1.0F;
    float nz = 0.0F;
    float u = 0.0F;
    float v = 0.0F;
    float face_index = 0.0F;
};

static_assert(sizeof(StylizedPrimitiveVertex) == sizeof(float) * 9U);

enum class StylizedPrimitiveType : std::uint8_t {
    RoundedBox = 0,
    Capsule,
    Ellipsoid,
    TaperedCylinder,
    Panel,
    Ribbon,
};

enum class StylizedPrimitiveLod : std::uint8_t {
    Low = 0,
    Medium,
    High,
};

struct StylizedPrimitiveBounds {
    glm::vec3 min {0.0F};
    glm::vec3 max {0.0F};
};

struct StylizedPrimitiveMesh {
    std::vector<StylizedPrimitiveVertex> vertices;
    std::vector<std::uint32_t> indices;
    StylizedPrimitiveBounds bounds {};

    [[nodiscard]] auto empty() const noexcept -> bool {
        return vertices.empty() || indices.empty();
    }

    [[nodiscard]] auto triangle_count() const noexcept -> std::size_t {
        return indices.size() / 3U;
    }
};

// Je produis des gabarits canoniques centres a l'origine. Ils restent tous
// contenus dans [-0,5 ; 0,5] et sont ensuite transformes par les instances.
//
// Bornes particulieres :
// - Capsule : X/Z = +/- kStylizedCapsuleRadius, Y = +/- 0,5.
// - Panel : X/Y = +/- 0,5, Z = [0 ; kStylizedPanelBow].
// - Ribbon : X = +/- 0,5, Y = +/- kStylizedRibbonHalfWidth,
//            Z = +/- kStylizedRibbonWaveAmplitude.
inline constexpr float kStylizedRoundedBoxRadius = 0.16F;
inline constexpr float kStylizedCapsuleRadius = 0.32F;
inline constexpr float kStylizedCapsuleCylinderHalfHeight = 0.18F;
inline constexpr float kStylizedTaperedCylinderTopRadius = 0.36F;
inline constexpr float kStylizedPanelBow = 0.08F;
inline constexpr float kStylizedRibbonHalfWidth = 0.09F;
inline constexpr float kStylizedRibbonWaveAmplitude = 0.06F;

[[nodiscard]] auto build_stylized_rounded_box(StylizedPrimitiveLod lod) -> StylizedPrimitiveMesh;
[[nodiscard]] auto build_stylized_capsule(StylizedPrimitiveLod lod) -> StylizedPrimitiveMesh;
[[nodiscard]] auto build_stylized_ellipsoid(StylizedPrimitiveLod lod) -> StylizedPrimitiveMesh;
[[nodiscard]] auto build_stylized_tapered_cylinder(StylizedPrimitiveLod lod) -> StylizedPrimitiveMesh;
[[nodiscard]] auto build_stylized_panel(StylizedPrimitiveLod lod) -> StylizedPrimitiveMesh;
[[nodiscard]] auto build_stylized_ribbon(StylizedPrimitiveLod lod) -> StylizedPrimitiveMesh;

[[nodiscard]] auto build_stylized_primitive(
    StylizedPrimitiveType type,
    StylizedPrimitiveLod lod) -> StylizedPrimitiveMesh;

} // namespace valcraft
