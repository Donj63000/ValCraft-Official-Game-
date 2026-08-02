#include "render/VisualEntityPrimitives.h"

#include "player/PlayerGeometry.h"

#include <glm/geometric.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/vec4.hpp>

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <limits>

namespace valcraft {

namespace {

constexpr float kAtlasTilesPerAxis = 8.0F;
constexpr float kAtlasUvTolerance = 2.0e-3F;
constexpr float kDimensionEpsilon = 1.0e-5F;
constexpr float kLinearRatio = 1.75F;
constexpr float kFlatRatio = 0.28F;
constexpr float kOrganicLongitudinalRatio = 1.42F;
constexpr float kCompactAccentMaximumExtent = 0.075F;
constexpr float kCreatureLimbWidthScale = 1.10F;
constexpr float kHumanoidLimbWidthScale = 1.07F;

[[nodiscard]] constexpr auto primitive_index(
    StylizedPrimitiveType primitive) noexcept -> std::size_t {
    switch (primitive) {
    case StylizedPrimitiveType::RoundedBox:
        return 0U;
    case StylizedPrimitiveType::Capsule:
        return 1U;
    case StylizedPrimitiveType::Ellipsoid:
        return 2U;
    case StylizedPrimitiveType::TaperedCylinder:
        return 3U;
    case StylizedPrimitiveType::Panel:
        return 4U;
    case StylizedPrimitiveType::Ribbon:
        return 5U;
    }
    return 0U;
}

[[nodiscard]] constexpr auto safe_primitive(
    StylizedPrimitiveType primitive) noexcept -> StylizedPrimitiveType {
    switch (primitive) {
    case StylizedPrimitiveType::RoundedBox:
    case StylizedPrimitiveType::Capsule:
    case StylizedPrimitiveType::Ellipsoid:
    case StylizedPrimitiveType::TaperedCylinder:
    case StylizedPrimitiveType::Panel:
    case StylizedPrimitiveType::Ribbon:
        return primitive;
    }
    return StylizedPrimitiveType::RoundedBox;
}

[[nodiscard]] constexpr auto lod_index(
    StylizedPrimitiveLod lod) noexcept -> std::size_t {
    switch (lod) {
    case StylizedPrimitiveLod::Low:
        return 0U;
    case StylizedPrimitiveLod::Medium:
        return 1U;
    case StylizedPrimitiveLod::High:
        return 2U;
    }
    return 1U;
}

[[nodiscard]] constexpr auto safe_lod(
    StylizedPrimitiveLod lod) noexcept -> StylizedPrimitiveLod {
    switch (lod) {
    case StylizedPrimitiveLod::Low:
    case StylizedPrimitiveLod::Medium:
    case StylizedPrimitiveLod::High:
        return lod;
    }
    return StylizedPrimitiveLod::Medium;
}

[[nodiscard]] constexpr auto cache_index(
    StylizedPrimitiveType primitive,
    StylizedPrimitiveLod lod) noexcept -> std::size_t {
    return lod_index(lod) * kVisualEntityPrimitiveTypeCount +
           primitive_index(primitive);
}

[[nodiscard]] constexpr auto tile_bit(std::uint8_t tile) noexcept
    -> std::uint64_t {
    return tile < 64U ? (std::uint64_t {1U} << tile) : 0U;
}

template <typename Enum>
[[nodiscard]] constexpr auto tile_bit(Enum tile) noexcept -> std::uint64_t {
    return tile_bit(static_cast<std::uint8_t>(tile));
}

template <typename... Enum>
[[nodiscard]] constexpr auto tile_bits(Enum... tiles) noexcept
    -> std::uint64_t {
    return (std::uint64_t {0U} | ... | tile_bit(tiles));
}

[[nodiscard]] auto contains_any(
    std::uint64_t value,
    std::uint64_t candidates) noexcept -> bool {
    return (value & candidates) != 0U;
}

[[nodiscard]] auto finite_vec3(const glm::vec3& value) noexcept -> bool {
    return std::isfinite(value.x) &&
           std::isfinite(value.y) &&
           std::isfinite(value.z);
}

[[nodiscard]] auto part_dimensions(
    const CreaturePartInstance& part,
    bool& valid) noexcept -> glm::vec3 {
    const glm::vec3 x_axis {part.transform[0]};
    const glm::vec3 y_axis {part.transform[1]};
    const glm::vec3 z_axis {part.transform[2]};
    const glm::vec3 translation {part.transform[3]};
    const auto dimensions = glm::vec3 {
        glm::length(x_axis),
        glm::length(y_axis),
        glm::length(z_axis),
    };
    valid = finite_vec3(dimensions) &&
            finite_vec3(translation) &&
            dimensions.x > kDimensionEpsilon &&
            dimensions.y > kDimensionEpsilon &&
            dimensions.z > kDimensionEpsilon;
    return valid ? dimensions : glm::vec3 {1.0F};
}

[[nodiscard]] auto axis_vector(VisualEntityLocalAxis axis) noexcept
    -> glm::vec3 {
    switch (axis) {
    case VisualEntityLocalAxis::X:
        return {1.0F, 0.0F, 0.0F};
    case VisualEntityLocalAxis::Y:
        return {0.0F, 1.0F, 0.0F};
    case VisualEntityLocalAxis::Z:
        return {0.0F, 0.0F, 1.0F};
    }
    return {0.0F, 1.0F, 0.0F};
}

[[nodiscard]] auto major_axis_of(const glm::vec3& dimensions) noexcept
    -> VisualEntityLocalAxis {
    if (dimensions.x >= dimensions.y && dimensions.x >= dimensions.z) {
        return VisualEntityLocalAxis::X;
    }
    if (dimensions.y >= dimensions.z) {
        return VisualEntityLocalAxis::Y;
    }
    return VisualEntityLocalAxis::Z;
}

[[nodiscard]] auto minor_axis_of(const glm::vec3& dimensions) noexcept
    -> VisualEntityLocalAxis {
    if (dimensions.x <= dimensions.y && dimensions.x <= dimensions.z) {
        return VisualEntityLocalAxis::X;
    }
    if (dimensions.y <= dimensions.z) {
        return VisualEntityLocalAxis::Y;
    }
    return VisualEntityLocalAxis::Z;
}

[[nodiscard]] auto sorted_dimensions(const glm::vec3& dimensions) noexcept
    -> std::array<float, 3> {
    auto sorted = std::array<float, 3> {
        dimensions.x,
        dimensions.y,
        dimensions.z,
    };
    std::sort(sorted.begin(), sorted.end(), std::greater<float> {});
    return sorted;
}

[[nodiscard]] auto axis_alignment_from_y(
    VisualEntityLocalAxis target) noexcept -> glm::mat4 {
    auto result = glm::mat4 {1.0F};
    switch (target) {
    case VisualEntityLocalAxis::X:
        // Je conserve une base directe : Y canonique devient X local.
        result[0] = glm::vec4 {0.0F, -1.0F, 0.0F, 0.0F};
        result[1] = glm::vec4 {1.0F, 0.0F, 0.0F, 0.0F};
        result[2] = glm::vec4 {0.0F, 0.0F, 1.0F, 0.0F};
        break;
    case VisualEntityLocalAxis::Y:
        break;
    case VisualEntityLocalAxis::Z:
        result[0] = glm::vec4 {1.0F, 0.0F, 0.0F, 0.0F};
        result[1] = glm::vec4 {0.0F, 0.0F, 1.0F, 0.0F};
        result[2] = glm::vec4 {0.0F, -1.0F, 0.0F, 0.0F};
        break;
    }
    return result;
}

[[nodiscard]] auto planar_alignment(
    VisualEntityLocalAxis major,
    VisualEntityLocalAxis minor) noexcept -> glm::mat4 {
    const auto major_vector = axis_vector(major);
    const auto minor_vector = axis_vector(minor);
    const auto remaining_vector = glm::cross(minor_vector, major_vector);
    if (glm::dot(remaining_vector, remaining_vector) <= 0.5F) {
        return glm::mat4 {1.0F};
    }

    auto result = glm::mat4 {1.0F};
    // Le panneau canonique est long sur X et mince sur Z.
    result[0] = glm::vec4 {major_vector, 0.0F};
    result[1] = glm::vec4 {remaining_vector, 0.0F};
    result[2] = glm::vec4 {minor_vector, 0.0F};
    return result;
}

[[nodiscard]] auto primitive_alignment(
    StylizedPrimitiveType primitive,
    VisualEntityLocalAxis major,
    VisualEntityLocalAxis minor) noexcept -> glm::mat4 {
    switch (primitive) {
    case StylizedPrimitiveType::Capsule:
    case StylizedPrimitiveType::TaperedCylinder:
        return axis_alignment_from_y(major);
    case StylizedPrimitiveType::Panel:
    case StylizedPrimitiveType::Ribbon:
        return planar_alignment(major, minor);
    case StylizedPrimitiveType::RoundedBox:
    case StylizedPrimitiveType::Ellipsoid:
        return glm::mat4 {1.0F};
    }
    return glm::mat4 {1.0F};
}

[[nodiscard]] auto primitive_bounds_compensation(
    StylizedPrimitiveType primitive,
    VisualEntitySemanticRole role,
    VisualEntityContext context) noexcept -> glm::mat4 {
    if (primitive != StylizedPrimitiveType::Capsule) {
        return glm::mat4 {1.0F};
    }

    // Je compense ici le rayon canonique de la capsule. Sans cette mise a
    // l'echelle, chaque membre moderne ne remplit que 64 % de la largeur du
    // volume historique. Je lui ajoute ensuite une marge contextuelle bornee :
    // elle renforce la silhouette sans deplacer le rig ni ses articulations.
    auto silhouette_scale = 1.0F;
    if (role == VisualEntitySemanticRole::Limb) {
        switch (context) {
        case VisualEntityContext::Creature:
            silhouette_scale = kCreatureLimbWidthScale;
            break;
        case VisualEntityContext::Crew:
        case VisualEntityContext::PlayerWorld:
        case VisualEntityContext::PlayerViewModel:
            silhouette_scale = kHumanoidLimbWidthScale;
            break;
        case VisualEntityContext::Generic:
            break;
        }
    }
    const auto transverse_scale =
        (0.5F / kStylizedCapsuleRadius) * silhouette_scale;
    return glm::scale(
        glm::mat4 {1.0F},
        glm::vec3 {transverse_scale, 1.0F, transverse_scale});
}

[[nodiscard]] auto primitive_to_part_local(
    StylizedPrimitiveType primitive,
    VisualEntityLocalAxis major,
    VisualEntityLocalAxis minor,
    VisualEntitySemanticRole role,
    VisualEntityContext context) noexcept -> glm::mat4 {
    // Je compense le gabarit avant de l'orienter : ses deux axes transverses
    // restent donc ceux de la capsule, quelle que soit l'orientation du membre.
    return primitive_alignment(primitive, major, minor) *
           primitive_bounds_compensation(primitive, role, context);
}

struct SemanticHints {
    bool organic = false;
    bool horn_or_claw = false;
    bool tool_shaft = false;
    bool blade = false;
    bool cloth = false;
    bool flexible = false;
    bool soft_panel = false;
    bool rounded_terminal = false;
};

[[nodiscard]] auto creature_hints(std::uint64_t tiles) noexcept
    -> SemanticHints {
    constexpr auto horns_and_claws = tile_bits(
        CreatureAtlasTile::CowHorn,
        CreatureAtlasTile::ZombieHorn,
        CreatureAtlasTile::ZombieClaw,
        CreatureAtlasTile::ZombieTeeth);
    constexpr auto shafts = tile_bits(
        CreatureAtlasTile::CrewRope,
        CreatureAtlasTile::CrewWood,
        CreatureAtlasTile::CrewIron);
    constexpr auto cloth = tile_bits(
        CreatureAtlasTile::VillagerCloth,
        CreatureAtlasTile::VillagerApron,
        CreatureAtlasTile::CrewNavyCloth,
        CreatureAtlasTile::CrewIvoryCloth,
        CreatureAtlasTile::CrewStripedCloth,
        CreatureAtlasTile::CrewOchreCloth,
        CreatureAtlasTile::CrewRedCloth,
        CreatureAtlasTile::CrewBurgundyCloth,
        CreatureAtlasTile::CrewCanvas,
        CreatureAtlasTile::MarlowUniform,
        CreatureAtlasTile::MarlowSwimCap);
    constexpr auto flexible = tile_bits(
        CreatureAtlasTile::ZombieVein,
        CreatureAtlasTile::ZombieScar,
        CreatureAtlasTile::VillagerApron,
        CreatureAtlasTile::CrewRope,
        CreatureAtlasTile::CrewCanvas);
    constexpr auto soft_panels = tile_bits(
        CreatureAtlasTile::PigEar);
    constexpr auto hard = horns_and_claws | shafts | tile_bits(
        CreatureAtlasTile::PigHoof,
        CreatureAtlasTile::CowHoof,
        CreatureAtlasTile::SheepHoof,
        CreatureAtlasTile::ZombieBone,
        CreatureAtlasTile::CrewLeather,
        CreatureAtlasTile::CrewGold);
    constexpr auto all_known =
        (std::uint64_t {1U} << static_cast<std::uint8_t>(
             CreatureAtlasTile::Count)) -
        1U;

    return SemanticHints {
        contains_any(tiles, all_known & ~hard & ~cloth),
        contains_any(tiles, horns_and_claws),
        contains_any(tiles, shafts),
        false,
        contains_any(tiles, cloth),
        contains_any(tiles, flexible),
        contains_any(tiles, soft_panels),
        false,
    };
}

[[nodiscard]] auto player_hints(std::uint64_t tiles) noexcept
    -> SemanticHints {
    constexpr auto blade = tile_bits(
        PlayerAtlasTile::SwordBlade,
        PlayerAtlasTile::SwordEdge);
    constexpr auto shafts = tile_bits(
        PlayerAtlasTile::SwordGrip,
        PlayerAtlasTile::SwordPommel);
    constexpr auto cloth = tile_bits(
        PlayerAtlasTile::Shirt,
        PlayerAtlasTile::Pants,
        PlayerAtlasTile::ShirtShadow,
        PlayerAtlasTile::PantsShadow,
        PlayerAtlasTile::Sleeve,
        PlayerAtlasTile::Belt);
    constexpr auto organic = tile_bits(
        PlayerAtlasTile::Skin,
        PlayerAtlasTile::Hair,
        PlayerAtlasTile::Eye,
        PlayerAtlasTile::Mouth,
        PlayerAtlasTile::Hurt,
        PlayerAtlasTile::SkinShadow,
        PlayerAtlasTile::HairShadow,
        PlayerAtlasTile::Face);
    constexpr auto rounded_terminals = tile_bits(
        PlayerAtlasTile::Shoes,
        PlayerAtlasTile::Sole);
    return SemanticHints {
        contains_any(tiles, organic),
        false,
        contains_any(tiles, shafts),
        contains_any(tiles, blade),
        contains_any(tiles, cloth),
        false,
        false,
        contains_any(tiles, rounded_terminals),
    };
}

[[nodiscard]] auto semantic_hints(
    const CreaturePartInstance& part,
    VisualEntityContext context,
    std::uint64_t tiles) noexcept -> SemanticHints {
    switch (context) {
    case VisualEntityContext::Creature:
    case VisualEntityContext::Crew:
        return creature_hints(tiles);
    case VisualEntityContext::PlayerWorld:
    case VisualEntityContext::PlayerViewModel:
        return player_hints(tiles);
    case VisualEntityContext::Generic:
        break;
    }

    const auto material =
        std::isfinite(part.material_class) ? part.material_class : 1.0F;
    return SemanticHints {
        material < 0.50F,
        false,
        material >= 0.50F && material < 0.72F,
        false,
        material >= 0.16F && material < 0.31F,
        false,
        false,
        false,
    };
}

[[nodiscard]] auto select_role(
    const SemanticHints& hints,
    bool linear,
    bool compact_accent) noexcept -> VisualEntitySemanticRole {
    if (hints.blade) {
        return VisualEntitySemanticRole::Blade;
    }
    if (hints.horn_or_claw) {
        return VisualEntitySemanticRole::HornOrClaw;
    }
    if (hints.tool_shaft) {
        return VisualEntitySemanticRole::ToolShaft;
    }
    if (hints.flexible || hints.soft_panel) {
        return VisualEntitySemanticRole::FlexibleDetail;
    }
    if (compact_accent) {
        return VisualEntitySemanticRole::HeadOrJoint;
    }
    if (hints.rounded_terminal) {
        return VisualEntitySemanticRole::HardProp;
    }
    if (hints.cloth) {
        return linear
                   ? VisualEntitySemanticRole::Limb
                   : VisualEntitySemanticRole::ClothPanel;
    }
    if (hints.organic) {
        return linear
                   ? VisualEntitySemanticRole::Limb
                   : VisualEntitySemanticRole::OrganicMass;
    }
    return VisualEntitySemanticRole::HardProp;
}

[[nodiscard]] auto select_primitive(
    const SemanticHints& hints,
    bool linear,
    bool flat,
    bool organic_longitudinal,
    bool compact_accent) noexcept -> StylizedPrimitiveType {
    if (hints.blade) {
        return StylizedPrimitiveType::Panel;
    }
    if (hints.horn_or_claw) {
        return linear
                   ? StylizedPrimitiveType::TaperedCylinder
                   : StylizedPrimitiveType::Ellipsoid;
    }
    if (hints.tool_shaft) {
        return linear
                   ? StylizedPrimitiveType::TaperedCylinder
                   : StylizedPrimitiveType::RoundedBox;
    }
    if (hints.soft_panel) {
        return StylizedPrimitiveType::Panel;
    }
    if (hints.flexible && linear) {
        return StylizedPrimitiveType::Ribbon;
    }
    if (hints.flexible && flat) {
        return StylizedPrimitiveType::Panel;
    }
    if (hints.cloth && flat) {
        return linear
                   ? StylizedPrimitiveType::Ribbon
                   : StylizedPrimitiveType::Panel;
    }
    if (hints.cloth) {
        return linear
                   ? StylizedPrimitiveType::Capsule
                   : StylizedPrimitiveType::Ellipsoid;
    }
    if (hints.rounded_terminal || compact_accent) {
        return StylizedPrimitiveType::Ellipsoid;
    }
    if (hints.organic) {
        if (flat) {
            return StylizedPrimitiveType::Panel;
        }
        return linear || organic_longitudinal
                   ? StylizedPrimitiveType::Capsule
                   : StylizedPrimitiveType::Ellipsoid;
    }
    if (flat) {
        return StylizedPrimitiveType::Panel;
    }
    if (linear) {
        return StylizedPrimitiveType::TaperedCylinder;
    }
    return StylizedPrimitiveType::RoundedBox;
}

void hash_byte(std::uint64_t& hash, std::uint8_t value) noexcept {
    constexpr std::uint64_t kFnvPrime = 1099511628211ULL;
    hash ^= value;
    hash *= kFnvPrime;
}

void hash_u32(std::uint64_t& hash, std::uint32_t value) noexcept {
    for (unsigned int shift = 0U; shift < 32U; shift += 8U) {
        hash_byte(
            hash,
            static_cast<std::uint8_t>((value >> shift) & 0xFFU));
    }
}

void hash_float(std::uint64_t& hash, float value) noexcept {
    hash_u32(hash, std::bit_cast<std::uint32_t>(value));
}

void hash_mesh(
    std::uint64_t& hash,
    const StylizedPrimitiveMesh& mesh) noexcept {
    hash_u32(hash, static_cast<std::uint32_t>(mesh.vertices.size()));
    hash_u32(hash, static_cast<std::uint32_t>(mesh.indices.size()));
    for (const auto& vertex : mesh.vertices) {
        hash_float(hash, vertex.x);
        hash_float(hash, vertex.y);
        hash_float(hash, vertex.z);
        hash_float(hash, vertex.nx);
        hash_float(hash, vertex.ny);
        hash_float(hash, vertex.nz);
        hash_float(hash, vertex.u);
        hash_float(hash, vertex.v);
        hash_float(hash, vertex.face_index);
    }
    for (const auto index : mesh.indices) {
        hash_u32(hash, index);
    }
    hash_float(hash, mesh.bounds.min.x);
    hash_float(hash, mesh.bounds.min.y);
    hash_float(hash, mesh.bounds.min.z);
    hash_float(hash, mesh.bounds.max.x);
    hash_float(hash, mesh.bounds.max.y);
    hash_float(hash, mesh.bounds.max.z);
}

} // namespace

VisualEntityPrimitiveCache::VisualEntityPrimitiveCache() {
    constexpr std::array<StylizedPrimitiveType,
                         kVisualEntityPrimitiveTypeCount>
        primitives {{
            StylizedPrimitiveType::RoundedBox,
            StylizedPrimitiveType::Capsule,
            StylizedPrimitiveType::Ellipsoid,
            StylizedPrimitiveType::TaperedCylinder,
            StylizedPrimitiveType::Panel,
            StylizedPrimitiveType::Ribbon,
        }};
    constexpr std::array<StylizedPrimitiveLod, kVisualEntityPrimitiveLodCount>
        lods {{
            StylizedPrimitiveLod::Low,
            StylizedPrimitiveLod::Medium,
            StylizedPrimitiveLod::High,
        }};

    for (const auto lod : lods) {
        for (const auto primitive : primitives) {
            meshes_[cache_index(primitive, lod)] =
                build_stylized_primitive(primitive, lod);
        }
    }

    constexpr std::uint64_t kFnvOffset = 14695981039346656037ULL;
    fingerprint_ = kFnvOffset;
    for (const auto& mesh_value : meshes_) {
        hash_mesh(fingerprint_, mesh_value);
    }
}

auto VisualEntityPrimitiveCache::mesh(
    StylizedPrimitiveType primitive,
    StylizedPrimitiveLod lod) const noexcept -> const StylizedPrimitiveMesh& {
    const auto normalized_primitive = safe_primitive(primitive);
    const auto normalized_lod = safe_lod(lod);
    return meshes_[cache_index(normalized_primitive, normalized_lod)];
}

auto VisualEntityPrimitiveCache::fingerprint() const noexcept
    -> std::uint64_t {
    return fingerprint_;
}

auto visual_entity_primitive_cache() noexcept
    -> const VisualEntityPrimitiveCache& {
    static const VisualEntityPrimitiveCache cache {};
    return cache;
}

auto visual_entity_atlas_tile_mask(
    const CreaturePartInstance& part) noexcept -> std::uint64_t {
    std::uint64_t result = 0U;
    constexpr auto atlas_step = 1.0F / kAtlasTilesPerAxis;
    for (const auto& face : part.face_uvs) {
        if (!std::isfinite(face.u0) ||
            !std::isfinite(face.v0) ||
            !std::isfinite(face.u1) ||
            !std::isfinite(face.v1)) {
            continue;
        }

        const auto u_min = std::min(face.u0, face.u1);
        const auto v_min = std::min(face.v0, face.v1);
        const auto width = std::abs(face.u1 - face.u0);
        const auto height = std::abs(face.v1 - face.v0);
        const auto tile_x = static_cast<int>(
            std::lround(u_min * kAtlasTilesPerAxis));
        const auto tile_y = static_cast<int>(
            std::lround(v_min * kAtlasTilesPerAxis));
        if (tile_x < 0 || tile_x >= 8 ||
            tile_y < 0 || tile_y >= 8 ||
            std::abs(u_min - static_cast<float>(tile_x) * atlas_step) >
                kAtlasUvTolerance ||
            std::abs(v_min - static_cast<float>(tile_y) * atlas_step) >
                kAtlasUvTolerance ||
            std::abs(width - atlas_step) > kAtlasUvTolerance ||
            std::abs(height - atlas_step) > kAtlasUvTolerance) {
            continue;
        }

        const auto tile = static_cast<std::uint8_t>(tile_y * 8 + tile_x);
        result |= tile_bit(tile);
    }
    return result;
}

auto classify_visual_entity_part(
    const CreaturePartInstance& part,
    VisualEntityContext context) noexcept -> VisualEntityPrimitiveClassification {
    bool valid_transform = false;
    const auto dimensions = part_dimensions(part, valid_transform);
    const auto major_axis = major_axis_of(dimensions);
    const auto minor_axis = minor_axis_of(dimensions);
    if (!valid_transform) {
        return VisualEntityPrimitiveClassification {
            StylizedPrimitiveType::RoundedBox,
            VisualEntitySemanticRole::HardProp,
            major_axis,
            minor_axis,
            dimensions,
            glm::mat4 {1.0F},
            false,
        };
    }

    const auto sorted = sorted_dimensions(dimensions);
    const auto linear =
        sorted[0] >= std::max(sorted[1], kDimensionEpsilon) * kLinearRatio;
    const auto flat =
        sorted[2] <= std::max(sorted[1], kDimensionEpsilon) * kFlatRatio;
    const auto tiles = visual_entity_atlas_tile_mask(part);
    const auto hints = semantic_hints(part, context, tiles);
    const auto organic_longitudinal =
        hints.organic &&
        !flat &&
        sorted[0] >=
            std::max(sorted[1], kDimensionEpsilon) *
                kOrganicLongitudinalRatio;
    const auto compact_accent =
        context != VisualEntityContext::Generic &&
        !hints.organic &&
        !hints.cloth &&
        !hints.horn_or_claw &&
        !hints.tool_shaft &&
        !hints.blade &&
        sorted[0] <= kCompactAccentMaximumExtent;
    const auto primitive = select_primitive(
        hints,
        linear,
        flat,
        organic_longitudinal,
        compact_accent);
    auto role = select_role(hints, linear, compact_accent);
    if (hints.organic && flat) {
        role = VisualEntitySemanticRole::FlexibleDetail;
    }
    if (hints.organic && !linear && sorted[0] <= sorted[1] * 1.28F) {
        role = VisualEntitySemanticRole::HeadOrJoint;
    }

    return VisualEntityPrimitiveClassification {
        primitive,
        role,
        major_axis,
        minor_axis,
        dimensions,
        primitive_to_part_local(
            primitive,
            major_axis,
            minor_axis,
            role,
            context),
        true,
    };
}

auto build_visual_entity_primitive_instances(
    std::span<const CreaturePartInstance> parts,
    VisualEntityContext context) -> std::vector<VisualEntityPrimitiveInstance> {
    std::vector<VisualEntityPrimitiveInstance> output {};
    output.reserve(parts.size());
    for (std::size_t index = 0U; index < parts.size(); ++index) {
        const auto classification =
            classify_visual_entity_part(parts[index], context);
        output.push_back(VisualEntityPrimitiveInstance {
            parts[index],
            classification.primitive_to_part_local,
            classification.primitive,
            classification.role,
            index,
        });
    }
    return output;
}

auto select_visual_entity_primitive_lod(
    float distance_squared,
    float maximum_dimension,
    int available_lod_count,
    bool simplified_shadow,
    bool viewmodel) noexcept -> StylizedPrimitiveLod {
    auto lod = StylizedPrimitiveLod::Low;
    if (viewmodel) {
        lod = StylizedPrimitiveLod::High;
    } else if (!simplified_shadow &&
               std::isfinite(distance_squared)) {
        if (available_lod_count >= 3 &&
            distance_squared <= 18.0F * 18.0F) {
            lod = StylizedPrimitiveLod::High;
        } else if (
            available_lod_count >= 2 &&
            distance_squared <= 56.0F * 56.0F) {
            lod = StylizedPrimitiveLod::Medium;
        }
    }

    if (!std::isfinite(maximum_dimension) ||
        maximum_dimension < 0.060F) {
        return StylizedPrimitiveLod::Low;
    }
    if (maximum_dimension < 0.125F &&
        lod == StylizedPrimitiveLod::High) {
        return StylizedPrimitiveLod::Medium;
    }
    return lod;
}

auto visual_entity_part_casts_simplified_shadow(
    float maximum_dimension) noexcept -> bool {
    return std::isfinite(maximum_dimension) &&
           maximum_dimension >= 0.085F;
}

} // namespace valcraft
