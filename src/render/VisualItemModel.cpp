#include "render/VisualItemModel.h"

#include <glm/common.hpp>
#include <glm/geometric.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/vec3.hpp>
#include <glm/vec4.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <type_traits>

namespace valcraft {
namespace {

constexpr float kTransformQuantization = 1'000'000.0F;
constexpr float kGeometryQuantization = 1'000'000.0F;

[[nodiscard]] auto make_transform(
    const glm::vec3& translation,
    const glm::vec3& rotation_degrees,
    const glm::vec3& scale) noexcept -> glm::mat4 {
    auto transform = glm::translate(glm::mat4 {1.0F}, translation);
    transform = glm::rotate(
        transform,
        glm::radians(rotation_degrees.x),
        glm::vec3 {1.0F, 0.0F, 0.0F});
    transform = glm::rotate(
        transform,
        glm::radians(rotation_degrees.y),
        glm::vec3 {0.0F, 1.0F, 0.0F});
    transform = glm::rotate(
        transform,
        glm::radians(rotation_degrees.z),
        glm::vec3 {0.0F, 0.0F, 1.0F});
    return glm::scale(transform, scale);
}

void append_primitive(
    VisualItemModel& model,
    StylizedPrimitiveType primitive,
    VisualMaterialId material,
    const glm::vec3& translation,
    const glm::vec3& rotation_degrees,
    const glm::vec3& scale,
    const glm::vec4& tint = glm::vec4 {1.0F},
    bool two_sided = false) {
    model.primitives.push_back(VisualItemPrimitive {
        primitive,
        make_transform(translation, rotation_degrees, scale),
        material,
        tint,
        two_sided,
    });
}

void append_block(
    VisualItemModel& model,
    VisualMaterialId material,
    StylizedPrimitiveType primitive = StylizedPrimitiveType::RoundedBox,
    const glm::vec3& scale = glm::vec3 {1.0F}) {
    append_primitive(
        model,
        primitive,
        material,
        glm::vec3 {0.0F},
        glm::vec3 {0.0F},
        scale);
}

void append_tree_log(
    VisualItemModel& model,
    VisualMaterialId material) {
    append_primitive(
        model,
        StylizedPrimitiveType::TaperedCylinder,
        material,
        glm::vec3 {0.0F},
        glm::vec3 {0.0F, 0.0F, -8.0F},
        glm::vec3 {0.72F, 1.10F, 0.72F});
    append_primitive(
        model,
        StylizedPrimitiveType::Ellipsoid,
        material,
        glm::vec3 {0.0F, 0.48F, 0.0F},
        glm::vec3 {0.0F},
        glm::vec3 {0.82F, 0.18F, 0.82F},
        glm::vec4 {1.06F, 1.03F, 0.98F, 1.0F});
}

void append_leaf_cluster(
    VisualItemModel& model,
    VisualMaterialId material,
    bool pine) {
    if (pine) {
        for (int layer = 0; layer < 3; ++layer) {
            const auto layer_value = static_cast<float>(layer);
            append_primitive(
                model,
                StylizedPrimitiveType::Ellipsoid,
                material,
                glm::vec3 {0.0F, -0.30F + layer_value * 0.30F, 0.0F},
                glm::vec3 {0.0F, layer_value * 19.0F, 0.0F},
                glm::vec3 {
                    1.05F - layer_value * 0.22F,
                    0.40F,
                    1.05F - layer_value * 0.22F,
                },
                glm::vec4 {0.92F + layer_value * 0.03F, 1.0F, 0.94F, 1.0F},
                true);
        }
        return;
    }

    constexpr std::array<glm::vec3, 5> offsets {{
        {-0.26F, 0.02F, 0.00F},
        {0.25F, 0.08F, 0.03F},
        {0.00F, 0.28F, -0.08F},
        {-0.05F, -0.23F, 0.14F},
        {0.08F, -0.02F, -0.24F},
    }};
    for (std::size_t index = 0U; index < offsets.size(); ++index) {
        append_primitive(
            model,
            StylizedPrimitiveType::Ellipsoid,
            material,
            offsets[index],
            glm::vec3 {0.0F, static_cast<float>(index) * 29.0F, 0.0F},
            glm::vec3 {0.68F, 0.62F, 0.68F},
            glm::vec4 {
                0.94F + static_cast<float>(index % 2U) * 0.05F,
                1.0F,
                0.92F,
                1.0F,
            },
            true);
    }
}

void append_grass_bouquet(
    VisualItemModel& model,
    VisualMaterialId material) {
    constexpr std::array<float, 5> rotations {{-34.0F, -16.0F, 0.0F, 18.0F, 36.0F}};
    for (std::size_t index = 0U; index < rotations.size(); ++index) {
        append_primitive(
            model,
            StylizedPrimitiveType::Ribbon,
            material,
            glm::vec3 {
                (static_cast<float>(index) - 2.0F) * 0.12F,
                -0.02F + static_cast<float>(index % 2U) * 0.05F,
                (index % 2U == 0U) ? -0.08F : 0.08F,
            },
            glm::vec3 {0.0F, rotations[index], 78.0F + rotations[index] * 0.22F},
            glm::vec3 {0.86F, 0.72F, 0.70F},
            glm::vec4 {0.88F + static_cast<float>(index) * 0.025F, 1.0F, 0.92F, 1.0F},
            true);
    }
}

void append_flower(
    VisualItemModel& model,
    VisualMaterialId flower_material) {
    append_primitive(
        model,
        StylizedPrimitiveType::TaperedCylinder,
        VisualMaterialId::TallGrass,
        glm::vec3 {0.0F, -0.13F, 0.0F},
        glm::vec3 {0.0F},
        glm::vec3 {0.16F, 1.22F, 0.16F});
    constexpr std::array<float, 6> petal_rotations {{0.0F, 60.0F, 120.0F, 180.0F, 240.0F, 300.0F}};
    for (const auto rotation : petal_rotations) {
        const auto radians = glm::radians(rotation);
        append_primitive(
            model,
            StylizedPrimitiveType::Ellipsoid,
            flower_material,
            glm::vec3 {
                std::cos(radians) * 0.27F,
                0.49F,
                std::sin(radians) * 0.27F,
            },
            glm::vec3 {18.0F, -rotation, 0.0F},
            glm::vec3 {0.38F, 0.14F, 0.22F},
            glm::vec4 {1.0F},
            true);
    }
    append_primitive(
        model,
        StylizedPrimitiveType::Ellipsoid,
        VisualMaterialId::GoldenFlower,
        glm::vec3 {0.0F, 0.49F, 0.0F},
        glm::vec3 {0.0F},
        glm::vec3 {0.22F, 0.18F, 0.22F},
        glm::vec4 {1.0F},
        true);
}

void append_handle(
    VisualItemModel& model,
    const glm::vec3& translation,
    const glm::vec3& rotation_degrees,
    const glm::vec3& scale) {
    append_primitive(
        model,
        StylizedPrimitiveType::Capsule,
        VisualMaterialId::OakBark,
        translation,
        rotation_degrees,
        scale,
        glm::vec4 {0.90F, 0.82F, 0.70F, 1.0F});
}

[[nodiscard]] auto finite_transform(const glm::mat4& transform) noexcept -> bool {
    for (int column = 0; column < 4; ++column) {
        for (int row = 0; row < 4; ++row) {
            if (!std::isfinite(transform[column][row])) {
                return false;
            }
        }
    }
    return true;
}

void finish_model(VisualItemModel& model) {
    if (model.primitives.empty()) {
        model.bounds = {};
        model.geometry_checksum = 0U;
        return;
    }

    auto minimum = glm::vec3 {(std::numeric_limits<float>::max)()};
    auto maximum = glm::vec3 {(std::numeric_limits<float>::lowest)()};
    for (const auto& part : model.primitives) {
        if (!finite_transform(part.transform) ||
            part.material == VisualMaterialId::None) {
            model.primitives.clear();
            model.bounds = {};
            model.geometry_checksum = 0U;
            return;
        }
        const auto mesh = build_stylized_primitive(
            part.primitive,
            StylizedPrimitiveLod::Medium);
        for (const auto& vertex : mesh.vertices) {
            const auto transformed = part.transform *
                glm::vec4 {vertex.x, vertex.y, vertex.z, 1.0F};
            const auto position = glm::vec3 {transformed};
            minimum = glm::min(minimum, position);
            maximum = glm::max(maximum, position);
        }
    }
    model.bounds = {minimum, maximum};
    model.geometry_checksum = visual_item_model_fingerprint(model);
}

void hash_byte(std::uint64_t& checksum, std::uint8_t value) noexcept {
    checksum ^= static_cast<std::uint64_t>(value);
    checksum *= 1099511628211ULL;
}

template <typename Integer>
void hash_integer(std::uint64_t& checksum, Integer value) noexcept {
    using Unsigned = std::make_unsigned_t<Integer>;
    const auto unsigned_value = static_cast<Unsigned>(value);
    for (std::size_t byte = 0U; byte < sizeof(Unsigned); ++byte) {
        hash_byte(
            checksum,
            static_cast<std::uint8_t>(
                unsigned_value >>
                static_cast<unsigned int>(byte * 8U)));
    }
}

[[nodiscard]] auto quantized_float(
    float value,
    float scale) noexcept -> std::int32_t {
    if (!std::isfinite(value)) {
        return 0;
    }
    const auto scaled = static_cast<double>(value) *
                        static_cast<double>(scale);
    const auto clamped = std::clamp(
        scaled,
        static_cast<double>((std::numeric_limits<std::int32_t>::min)()),
        static_cast<double>((std::numeric_limits<std::int32_t>::max)()));
    return static_cast<std::int32_t>(std::llround(clamped));
}

} // namespace

auto build_visual_item_model(BlockId block_id) -> VisualItemModel {
    const auto item_id = canonical_visual_item_id(block_id);
    VisualItemModel model {};
    model.item_id = item_id;
    model.model_class = visual_item_model_class_for(item_id);
    if (!is_visual_item_displayable(block_id)) {
        return model;
    }

    // Je compose chaque objet avec la meme bibliotheque de primitives que les
    // entites modernes. Cette recette est partageable par les drops, les mains
    // et les icones ; elle ne contient aucune donnee de gameplay.
    switch (static_cast<BlockType>(item_id)) {
    case BlockType::Grass:
        append_block(
            model,
            VisualMaterialId::Loam,
            StylizedPrimitiveType::RoundedBox,
            glm::vec3 {1.0F, 0.90F, 1.0F});
        append_primitive(
            model,
            StylizedPrimitiveType::RoundedBox,
            VisualMaterialId::MeadowGrass,
            glm::vec3 {0.0F, 0.43F, 0.0F},
            glm::vec3 {0.0F},
            glm::vec3 {1.02F, 0.16F, 1.02F});
        break;
    case BlockType::Dirt:
        append_block(model, VisualMaterialId::Loam);
        break;
    case BlockType::Stone:
        append_block(
            model,
            VisualMaterialId::WarmStone,
            StylizedPrimitiveType::Ellipsoid,
            glm::vec3 {1.06F, 0.92F, 1.02F});
        break;
    case BlockType::Sand:
        append_block(
            model,
            VisualMaterialId::SunlitSand,
            StylizedPrimitiveType::RoundedBox,
            glm::vec3 {1.04F, 0.86F, 1.04F});
        break;
    case BlockType::Wood:
        append_tree_log(model, VisualMaterialId::OakBark);
        break;
    case BlockType::Leaves:
        append_leaf_cluster(model, VisualMaterialId::Broadleaf, false);
        break;
    case BlockType::Torch:
        append_primitive(
            model,
            StylizedPrimitiveType::TaperedCylinder,
            VisualMaterialId::OakBark,
            glm::vec3 {0.0F, -0.12F, 0.0F},
            glm::vec3 {0.0F, 0.0F, -5.0F},
            glm::vec3 {0.22F, 1.18F, 0.22F});
        append_primitive(
            model,
            StylizedPrimitiveType::Ellipsoid,
            VisualMaterialId::TorchFlame,
            glm::vec3 {0.0F, 0.49F, 0.0F},
            glm::vec3 {0.0F, 0.0F, 8.0F},
            glm::vec3 {0.38F, 0.58F, 0.38F},
            glm::vec4 {1.10F, 1.05F, 0.92F, 1.0F},
            true);
        break;
    case BlockType::Cobblestone:
        append_block(model, VisualMaterialId::Cobblestone);
        break;
    case BlockType::Planks:
        append_block(
            model,
            VisualMaterialId::TerracottaPlanks,
            StylizedPrimitiveType::RoundedBox,
            glm::vec3 {1.05F, 0.86F, 1.05F});
        break;
    case BlockType::Gravel: {
        constexpr std::array<glm::vec3, 5> offsets {{
            {-0.26F, -0.19F, 0.10F},
            {0.22F, -0.16F, 0.13F},
            {-0.08F, 0.15F, -0.15F},
            {0.28F, 0.18F, -0.08F},
            {-0.30F, 0.20F, 0.02F},
        }};
        for (std::size_t index = 0U; index < offsets.size(); ++index) {
            append_primitive(
                model,
                StylizedPrimitiveType::Ellipsoid,
                VisualMaterialId::RiverGravel,
                offsets[index],
                glm::vec3 {
                    static_cast<float>(index) * 13.0F,
                    static_cast<float>(index) * 31.0F,
                    static_cast<float>(index) * 7.0F,
                },
                glm::vec3 {0.58F, 0.48F, 0.56F});
        }
        break;
    }
    case BlockType::MossyStone:
        append_block(
            model,
            VisualMaterialId::MossyStone,
            StylizedPrimitiveType::RoundedBox,
            glm::vec3 {1.02F, 0.96F, 1.02F});
        break;
    case BlockType::Snow:
        append_block(
            model,
            VisualMaterialId::PowderSnow,
            StylizedPrimitiveType::RoundedBox,
            glm::vec3 {1.04F, 0.75F, 1.04F});
        append_primitive(
            model,
            StylizedPrimitiveType::Ellipsoid,
            VisualMaterialId::PowderSnow,
            glm::vec3 {0.08F, 0.39F, -0.03F},
            glm::vec3 {0.0F, 18.0F, 0.0F},
            glm::vec3 {0.92F, 0.22F, 0.86F});
        break;
    case BlockType::PineWood:
        append_tree_log(model, VisualMaterialId::PineBark);
        break;
    case BlockType::PineLeaves:
        append_leaf_cluster(model, VisualMaterialId::PineNeedles, true);
        break;
    case BlockType::TallGrass:
        append_grass_bouquet(model, VisualMaterialId::TallGrass);
        break;
    case BlockType::RedFlower:
        append_flower(model, VisualMaterialId::CrimsonFlower);
        break;
    case BlockType::YellowFlower:
        append_flower(model, VisualMaterialId::GoldenFlower);
        break;
    case BlockType::DeadShrub:
        for (int branch = -2; branch <= 2; ++branch) {
            append_primitive(
                model,
                StylizedPrimitiveType::Capsule,
                VisualMaterialId::DeadShrub,
                glm::vec3 {
                    static_cast<float>(branch) * 0.08F,
                    -0.05F,
                    static_cast<float>(std::abs(branch) % 2) * 0.06F,
                },
                glm::vec3 {
                    static_cast<float>(branch) * 8.0F,
                    static_cast<float>(branch) * 24.0F,
                    static_cast<float>(branch) * 17.0F,
                },
                glm::vec3 {0.16F, 1.12F, 0.16F},
                glm::vec4 {1.0F},
                true);
        }
        break;
    case BlockType::Cactus:
        append_primitive(
            model,
            StylizedPrimitiveType::TaperedCylinder,
            VisualMaterialId::CactusSkin,
            glm::vec3 {0.0F},
            glm::vec3 {0.0F, 0.0F, -2.0F},
            glm::vec3 {0.48F, 1.34F, 0.48F});
        append_primitive(
            model,
            StylizedPrimitiveType::Capsule,
            VisualMaterialId::CactusSkin,
            glm::vec3 {0.30F, 0.08F, 0.0F},
            glm::vec3 {0.0F, 0.0F, -58.0F},
            glm::vec3 {0.30F, 0.62F, 0.30F});
        append_primitive(
            model,
            StylizedPrimitiveType::Capsule,
            VisualMaterialId::CactusSkin,
            glm::vec3 {-0.29F, -0.10F, 0.03F},
            glm::vec3 {0.0F, 0.0F, 62.0F},
            glm::vec3 {0.28F, 0.54F, 0.28F});
        break;
    case BlockType::Water:
        append_block(
            model,
            VisualMaterialId::ClearWater,
            StylizedPrimitiveType::Ellipsoid,
            glm::vec3 {1.10F, 0.58F, 1.10F});
        break;
    case BlockType::Glass:
        append_block(model, VisualMaterialId::ClearGlass);
        break;
    case BlockType::Pastron:
        append_primitive(
            model,
            StylizedPrimitiveType::Panel,
            VisualMaterialId::BronzeArmor,
            glm::vec3 {0.0F, 0.02F, 0.0F},
            glm::vec3 {0.0F},
            glm::vec3 {0.90F, 1.12F, 0.52F});
        append_primitive(
            model,
            StylizedPrimitiveType::Ellipsoid,
            VisualMaterialId::BronzeArmor,
            glm::vec3 {-0.48F, 0.35F, 0.0F},
            glm::vec3 {0.0F},
            glm::vec3 {0.34F, 0.34F, 0.52F});
        append_primitive(
            model,
            StylizedPrimitiveType::Ellipsoid,
            VisualMaterialId::BronzeArmor,
            glm::vec3 {0.48F, 0.35F, 0.0F},
            glm::vec3 {0.0F},
            glm::vec3 {0.34F, 0.34F, 0.52F});
        break;
    case BlockType::RoundShield:
        append_primitive(
            model,
            StylizedPrimitiveType::Ellipsoid,
            VisualMaterialId::WoodShield,
            glm::vec3 {0.0F},
            glm::vec3 {0.0F, 0.0F, -8.0F},
            glm::vec3 {1.08F, 1.08F, 0.24F});
        append_primitive(
            model,
            StylizedPrimitiveType::Ellipsoid,
            VisualMaterialId::BronzeArmor,
            glm::vec3 {0.0F, 0.0F, 0.10F},
            glm::vec3 {0.0F},
            glm::vec3 {0.25F, 0.25F, 0.20F});
        break;
    case BlockType::Sword:
        append_handle(
            model,
            glm::vec3 {0.0F, -0.37F, 0.0F},
            glm::vec3 {0.0F, 0.0F, 0.0F},
            glm::vec3 {0.20F, 0.62F, 0.20F});
        append_primitive(
            model,
            StylizedPrimitiveType::Capsule,
            VisualMaterialId::BronzeArmor,
            glm::vec3 {0.0F, -0.12F, 0.0F},
            glm::vec3 {0.0F, 0.0F, 90.0F},
            glm::vec3 {0.20F, 0.82F, 0.20F});
        append_primitive(
            model,
            StylizedPrimitiveType::Panel,
            VisualMaterialId::ForgedSteel,
            glm::vec3 {0.0F, 0.35F, 0.0F},
            glm::vec3 {0.0F},
            glm::vec3 {0.30F, 1.30F, 0.18F},
            glm::vec4 {1.04F, 1.06F, 1.10F, 1.0F},
            true);
        break;
    case BlockType::Spear:
        append_handle(
            model,
            glm::vec3 {0.0F, -0.12F, 0.0F},
            glm::vec3 {0.0F, 0.0F, -6.0F},
            glm::vec3 {0.15F, 1.64F, 0.15F});
        append_primitive(
            model,
            StylizedPrimitiveType::Ellipsoid,
            VisualMaterialId::ForgedSteel,
            glm::vec3 {0.09F, 0.74F, 0.0F},
            glm::vec3 {0.0F, 0.0F, -6.0F},
            glm::vec3 {0.28F, 0.58F, 0.16F},
            glm::vec4 {1.05F, 1.07F, 1.10F, 1.0F});
        break;
    case BlockType::Shoes:
        for (const auto side : {-1.0F, 1.0F}) {
            append_primitive(
                model,
                StylizedPrimitiveType::RoundedBox,
                VisualMaterialId::Leather,
                glm::vec3 {side * 0.28F, -0.08F, 0.10F},
                glm::vec3 {0.0F, side * 6.0F, 0.0F},
                glm::vec3 {0.44F, 0.44F, 0.78F});
        }
        break;
    case BlockType::Pants:
        append_primitive(
            model,
            StylizedPrimitiveType::RoundedBox,
            VisualMaterialId::Leather,
            glm::vec3 {0.0F, 0.33F, 0.0F},
            glm::vec3 {0.0F},
            glm::vec3 {0.92F, 0.40F, 0.52F});
        for (const auto side : {-1.0F, 1.0F}) {
            append_primitive(
                model,
                StylizedPrimitiveType::Capsule,
                VisualMaterialId::Leather,
                glm::vec3 {side * 0.23F, -0.18F, 0.0F},
                glm::vec3 {0.0F, 0.0F, side * 3.0F},
                glm::vec3 {0.40F, 1.03F, 0.46F});
        }
        break;
    case BlockType::CoalOre:
    case BlockType::IronOre:
    case BlockType::GoldOre:
    case BlockType::DiamondOre:
    case BlockType::MetallicAlloyOre:
        append_block(
            model,
            visual_material_for_block(item_id),
            StylizedPrimitiveType::RoundedBox,
            glm::vec3 {1.03F, 0.96F, 1.03F});
        break;
    case BlockType::Pickaxe:
        append_handle(
            model,
            glm::vec3 {0.0F, -0.10F, 0.0F},
            glm::vec3 {0.0F, 0.0F, -12.0F},
            glm::vec3 {0.16F, 1.52F, 0.16F});
        append_primitive(
            model,
            StylizedPrimitiveType::Capsule,
            VisualMaterialId::ForgedSteel,
            glm::vec3 {0.14F, 0.56F, 0.0F},
            glm::vec3 {0.0F, 0.0F, 78.0F},
            glm::vec3 {0.22F, 1.15F, 0.20F});
        break;
    case BlockType::Axe:
        append_handle(
            model,
            glm::vec3 {-0.12F, -0.10F, 0.0F},
            glm::vec3 {0.0F, 0.0F, 9.0F},
            glm::vec3 {0.17F, 1.48F, 0.17F});
        append_primitive(
            model,
            StylizedPrimitiveType::Panel,
            VisualMaterialId::ForgedSteel,
            glm::vec3 {0.18F, 0.52F, 0.0F},
            glm::vec3 {0.0F, 0.0F, -10.0F},
            glm::vec3 {0.72F, 0.52F, 0.20F},
            glm::vec4 {1.02F, 1.03F, 1.05F, 1.0F},
            true);
        break;
    case BlockType::Shovel:
        append_handle(
            model,
            glm::vec3 {0.0F, 0.03F, 0.0F},
            glm::vec3 {0.0F, 0.0F, -6.0F},
            glm::vec3 {0.15F, 1.36F, 0.15F});
        append_primitive(
            model,
            StylizedPrimitiveType::Ellipsoid,
            VisualMaterialId::ForgedSteel,
            glm::vec3 {-0.08F, -0.61F, 0.0F},
            glm::vec3 {0.0F, 0.0F, -6.0F},
            glm::vec3 {0.52F, 0.62F, 0.18F},
            glm::vec4 {1.02F, 1.04F, 1.07F, 1.0F});
        break;
    case BlockType::Air:
    case BlockType::TorchWallPositiveX:
    case BlockType::TorchWallNegativeX:
    case BlockType::TorchWallPositiveZ:
    case BlockType::TorchWallNegativeZ:
    default:
        break;
    }

    finish_model(model);
    return model;
}

auto build_all_visual_item_models()
    -> std::array<VisualItemModel, kVisualItemModelCount> {
    std::array<VisualItemModel, kVisualItemModelCount> models {};
    for (std::size_t index = 0U; index < models.size(); ++index) {
        models[index] =
            build_visual_item_model(kVisualItemCanonicalIds[index]);
    }
    return models;
}

auto visual_item_model_fingerprint(
    const VisualItemModel& model) noexcept -> std::uint64_t {
    auto checksum = std::uint64_t {14695981039346656037ULL};
    hash_byte(checksum, model.item_id);
    hash_byte(
        checksum,
        static_cast<std::uint8_t>(model.model_class));
    hash_integer(checksum, static_cast<std::uint32_t>(model.primitives.size()));

    for (const auto& part : model.primitives) {
        hash_byte(
            checksum,
            static_cast<std::uint8_t>(part.primitive));
        hash_integer(
            checksum,
            static_cast<std::uint16_t>(part.material));
        hash_byte(checksum, part.two_sided ? 1U : 0U);
        for (int column = 0; column < 4; ++column) {
            for (int row = 0; row < 4; ++row) {
                hash_integer(
                    checksum,
                    quantized_float(
                        part.transform[column][row],
                        kTransformQuantization));
            }
        }
        for (int channel = 0; channel < 4; ++channel) {
            hash_integer(
                checksum,
                quantized_float(
                    part.albedo_tint[channel],
                    kTransformQuantization));
        }

        const auto mesh = build_stylized_primitive(
            part.primitive,
            StylizedPrimitiveLod::Medium);
        hash_integer(
            checksum,
            static_cast<std::uint32_t>(mesh.vertices.size()));
        hash_integer(
            checksum,
            static_cast<std::uint32_t>(mesh.indices.size()));
        for (const auto& vertex : mesh.vertices) {
            for (const auto value : {
                     vertex.x,
                     vertex.y,
                     vertex.z,
                     vertex.nx,
                     vertex.ny,
                     vertex.nz,
                     vertex.u,
                     vertex.v,
                     vertex.face_index,
                 }) {
                hash_integer(
                    checksum,
                    quantized_float(value, kGeometryQuantization));
            }
        }
        for (const auto index : mesh.indices) {
            hash_integer(checksum, index);
        }
    }
    return checksum;
}

} // namespace valcraft
