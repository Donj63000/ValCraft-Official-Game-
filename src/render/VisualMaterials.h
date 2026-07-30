#pragma once

#include "world/Block.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace valcraft {

enum class VisualSurfaceClass : std::uint8_t {
    Organic = 0,
    Architectural = 1,
    Proxy = 2,
    Cutout = 3,
    Liquid = 4,
};

enum class VisualMaterialId : std::uint16_t {
    None = 0,
    MeadowGrass = 1,
    Loam = 2,
    WarmStone = 3,
    SunlitSand = 4,
    OakBark = 5,
    Broadleaf = 6,
    TorchFlame = 7,
    Cobblestone = 8,
    TerracottaPlanks = 9,
    RiverGravel = 10,
    MossyStone = 11,
    PowderSnow = 12,
    PineBark = 13,
    PineNeedles = 14,
    TallGrass = 15,
    CrimsonFlower = 16,
    GoldenFlower = 17,
    DeadShrub = 18,
    CactusSkin = 19,
    ClearWater = 20,
    ClearGlass = 21,
    BronzeArmor = 22,
    WoodShield = 23,
    ForgedSteel = 24,
    Leather = 25,
    CoalOre = 26,
    IronOre = 27,
    GoldOre = 28,
    DiamondOre = 29,
    AlloyOre = 30,
    ToolWoodSteel = 31,
    ShipDarkHull = 32,
    ShipDeckOak = 33,
    ShipOiledOak = 34,
    ShipLinen = 35,
    ShipRope = 36,
    ShipIron = 37,
    ShipPatinatedBrass = 38,
    ShipLantern = 39,
    ShipGlass = 40,
    ShipNavyTextile = 41,
    ShipGold = 42,
    ShipBurgundyTextile = 43,
    ShipLeather = 44,
    ShipPaper = 45,
    ShipCeramic = 46,
    MarineSeagrass = 47,
    MarineKelp = 48,
    CoralWarm = 49,
    CoralLagoon = 50,
    CoralFan = 51,
    ReefFish = 52,
    MarineShell = 53,
    Count = 54,
};

inline constexpr std::uint16_t kInvalidVisualMaterialLayer = 0xFFFFU;
inline constexpr std::size_t kVisualMaterialCount =
    static_cast<std::size_t>(VisualMaterialId::Count);

struct VisualMaterialDefinition {
    VisualMaterialId id = VisualMaterialId::None;
    VisualSurfaceClass surface_class = VisualSurfaceClass::Proxy;
    std::uint16_t pack_layer = kInvalidVisualMaterialLayer;
    std::string_view name {};
    float texture_scale = 1.0F;
    float normal_strength = 1.0F;
    float macro_variation = 0.0F;
    bool alpha_tested = false;
    bool two_sided = false;
    bool emissive = false;
};

// Je garde cette table indépendante des BlockId : le gameplay peut ainsi
// conserver ses identifiants historiques pendant que le rendu évolue.
inline constexpr std::array<VisualMaterialDefinition, kVisualMaterialCount>
    kVisualMaterialDefinitions {{
        {VisualMaterialId::None, VisualSurfaceClass::Proxy, kInvalidVisualMaterialLayer, "none", 1.0F, 0.0F, 0.0F, false, false, false},
        {VisualMaterialId::MeadowGrass, VisualSurfaceClass::Organic, 0U, "meadow_grass", 0.72F, 0.82F, 0.24F, false, false, false},
        {VisualMaterialId::Loam, VisualSurfaceClass::Organic, 1U, "loam", 0.68F, 0.72F, 0.20F, false, false, false},
        {VisualMaterialId::WarmStone, VisualSurfaceClass::Organic, 2U, "warm_stone", 0.48F, 1.00F, 0.16F, false, false, false},
        {VisualMaterialId::SunlitSand, VisualSurfaceClass::Organic, 3U, "sunlit_sand", 0.62F, 0.54F, 0.28F, false, false, false},
        {VisualMaterialId::OakBark, VisualSurfaceClass::Proxy, 4U, "oak_bark", 0.84F, 0.96F, 0.18F, false, false, false},
        {VisualMaterialId::Broadleaf, VisualSurfaceClass::Cutout, 5U, "broadleaf", 0.92F, 0.62F, 0.30F, true, true, false},
        {VisualMaterialId::TorchFlame, VisualSurfaceClass::Proxy, 6U, "torch_flame", 1.00F, 0.16F, 0.08F, true, true, true},
        {VisualMaterialId::Cobblestone, VisualSurfaceClass::Architectural, 7U, "cobblestone", 0.52F, 1.00F, 0.12F, false, false, false},
        {VisualMaterialId::TerracottaPlanks, VisualSurfaceClass::Architectural, 8U, "terracotta_planks", 0.74F, 0.84F, 0.15F, false, false, false},
        {VisualMaterialId::RiverGravel, VisualSurfaceClass::Organic, 9U, "river_gravel", 0.58F, 0.92F, 0.20F, false, false, false},
        {VisualMaterialId::MossyStone, VisualSurfaceClass::Organic, 10U, "mossy_stone", 0.50F, 0.94F, 0.22F, false, false, false},
        {VisualMaterialId::PowderSnow, VisualSurfaceClass::Organic, 11U, "powder_snow", 0.78F, 0.32F, 0.16F, false, false, false},
        {VisualMaterialId::PineBark, VisualSurfaceClass::Proxy, 12U, "pine_bark", 0.80F, 1.00F, 0.16F, false, false, false},
        {VisualMaterialId::PineNeedles, VisualSurfaceClass::Cutout, 13U, "pine_needles", 0.94F, 0.72F, 0.26F, true, true, false},
        {VisualMaterialId::TallGrass, VisualSurfaceClass::Cutout, 14U, "tall_grass", 1.00F, 0.58F, 0.24F, true, true, false},
        {VisualMaterialId::CrimsonFlower, VisualSurfaceClass::Cutout, 15U, "crimson_flower", 1.00F, 0.46F, 0.22F, true, true, false},
        {VisualMaterialId::GoldenFlower, VisualSurfaceClass::Cutout, 16U, "golden_flower", 1.00F, 0.44F, 0.22F, true, true, false},
        {VisualMaterialId::DeadShrub, VisualSurfaceClass::Cutout, 17U, "dead_shrub", 0.92F, 0.92F, 0.14F, true, true, false},
        {VisualMaterialId::CactusSkin, VisualSurfaceClass::Proxy, 18U, "cactus_skin", 0.82F, 0.76F, 0.16F, false, false, false},
        {VisualMaterialId::ClearWater, VisualSurfaceClass::Liquid, 19U, "clear_water", 0.36F, 0.42F, 0.34F, false, true, false},
        {VisualMaterialId::ClearGlass, VisualSurfaceClass::Architectural, 20U, "clear_glass", 0.90F, 0.12F, 0.04F, false, true, false},
        {VisualMaterialId::BronzeArmor, VisualSurfaceClass::Proxy, 21U, "bronze_armor", 0.82F, 0.68F, 0.10F, false, false, false},
        {VisualMaterialId::WoodShield, VisualSurfaceClass::Proxy, 22U, "wood_shield", 0.78F, 0.86F, 0.14F, false, false, false},
        {VisualMaterialId::ForgedSteel, VisualSurfaceClass::Proxy, 23U, "forged_steel", 0.92F, 0.52F, 0.08F, false, false, false},
        {VisualMaterialId::Leather, VisualSurfaceClass::Proxy, 24U, "leather", 0.86F, 0.72F, 0.18F, false, false, false},
        {VisualMaterialId::CoalOre, VisualSurfaceClass::Organic, 25U, "coal_ore", 0.48F, 1.00F, 0.14F, false, false, false},
        {VisualMaterialId::IronOre, VisualSurfaceClass::Organic, 26U, "iron_ore", 0.48F, 0.92F, 0.14F, false, false, false},
        {VisualMaterialId::GoldOre, VisualSurfaceClass::Organic, 27U, "gold_ore", 0.48F, 0.82F, 0.16F, false, false, false},
        {VisualMaterialId::DiamondOre, VisualSurfaceClass::Organic, 28U, "diamond_ore", 0.48F, 0.70F, 0.20F, false, false, false},
        {VisualMaterialId::AlloyOre, VisualSurfaceClass::Organic, 29U, "alloy_ore", 0.48F, 0.72F, 0.18F, false, false, false},
        {VisualMaterialId::ToolWoodSteel, VisualSurfaceClass::Proxy, 30U, "tool_wood_steel", 0.84F, 0.66F, 0.10F, false, false, false},
        // Je réserve ces couches append-only au navire moderne afin de garder
        // intact le catalogue historique utilisé par le monde et le mode Legacy.
        {VisualMaterialId::ShipDarkHull, VisualSurfaceClass::Architectural, 31U, "ship_dark_hull", 0.54F, 1.00F, 0.16F, false, false, false},
        {VisualMaterialId::ShipDeckOak, VisualSurfaceClass::Architectural, 32U, "ship_deck_oak", 0.62F, 0.92F, 0.18F, false, false, false},
        {VisualMaterialId::ShipOiledOak, VisualSurfaceClass::Architectural, 33U, "ship_oiled_oak", 0.72F, 0.86F, 0.14F, false, false, false},
        {VisualMaterialId::ShipLinen, VisualSurfaceClass::Proxy, 34U, "ship_linen", 0.96F, 0.48F, 0.10F, false, true, false},
        {VisualMaterialId::ShipRope, VisualSurfaceClass::Proxy, 35U, "ship_rope", 0.70F, 0.96F, 0.12F, false, false, false},
        {VisualMaterialId::ShipIron, VisualSurfaceClass::Proxy, 36U, "ship_iron", 0.88F, 0.62F, 0.06F, false, false, false},
        {VisualMaterialId::ShipPatinatedBrass, VisualSurfaceClass::Proxy, 37U, "ship_patinated_brass", 0.82F, 0.70F, 0.12F, false, false, false},
        {VisualMaterialId::ShipLantern, VisualSurfaceClass::Proxy, 38U, "ship_lantern", 1.00F, 0.24F, 0.04F, false, false, true},
        {VisualMaterialId::ShipGlass, VisualSurfaceClass::Architectural, 39U, "ship_glass", 0.94F, 0.10F, 0.04F, false, true, false},
        {VisualMaterialId::ShipNavyTextile, VisualSurfaceClass::Proxy, 40U, "ship_navy_textile", 0.94F, 0.58F, 0.14F, false, true, false},
        {VisualMaterialId::ShipGold, VisualSurfaceClass::Proxy, 41U, "ship_gold", 0.82F, 0.56F, 0.08F, false, false, false},
        {VisualMaterialId::ShipBurgundyTextile, VisualSurfaceClass::Proxy, 42U, "ship_burgundy_textile", 0.94F, 0.58F, 0.14F, false, true, false},
        {VisualMaterialId::ShipLeather, VisualSurfaceClass::Proxy, 43U, "ship_leather", 0.86F, 0.68F, 0.14F, false, false, false},
        {VisualMaterialId::ShipPaper, VisualSurfaceClass::Proxy, 44U, "ship_paper", 1.08F, 0.22F, 0.10F, false, true, false},
        {VisualMaterialId::ShipCeramic, VisualSurfaceClass::Proxy, 45U, "ship_ceramic", 0.92F, 0.26F, 0.06F, false, false, false},
        // Je réserve ces couches au décor marin moderne. Elles ne deviennent
        // jamais des BlockId du monde et ne peuvent donc modifier une save.
        {VisualMaterialId::MarineSeagrass, VisualSurfaceClass::Cutout, 46U, "marine_seagrass", 1.00F, 0.46F, 0.22F, true, true, false},
        {VisualMaterialId::MarineKelp, VisualSurfaceClass::Cutout, 47U, "marine_kelp", 1.00F, 0.52F, 0.25F, true, true, false},
        {VisualMaterialId::CoralWarm, VisualSurfaceClass::Proxy, 48U, "coral_warm", 0.86F, 0.62F, 0.20F, false, false, false},
        {VisualMaterialId::CoralLagoon, VisualSurfaceClass::Proxy, 49U, "coral_lagoon", 0.86F, 0.58F, 0.20F, false, false, false},
        {VisualMaterialId::CoralFan, VisualSurfaceClass::Cutout, 50U, "coral_fan", 1.00F, 0.44F, 0.20F, true, true, false},
        {VisualMaterialId::ReefFish, VisualSurfaceClass::Cutout, 51U, "reef_fish", 1.00F, 0.38F, 0.18F, true, true, false},
        {VisualMaterialId::MarineShell, VisualSurfaceClass::Proxy, 52U, "marine_shell", 0.94F, 0.34F, 0.18F, false, false, false},
    }};

static_assert(
    static_cast<std::uint16_t>(VisualMaterialId::Count) <= 256U,
    "Je compacte l'identifiant visuel direct dans un octet de TerrainVertex");

[[nodiscard]] constexpr auto direct_visual_material_token(
    VisualMaterialId id) noexcept -> BlockId {
    return static_cast<BlockId>(static_cast<std::uint16_t>(id));
}

[[nodiscard]] constexpr auto is_known_visual_material_id(VisualMaterialId id) noexcept -> bool {
    return static_cast<std::uint16_t>(id) < static_cast<std::uint16_t>(VisualMaterialId::Count);
}

[[nodiscard]] constexpr auto visual_material_definition(VisualMaterialId id) noexcept
    -> const VisualMaterialDefinition& {
    if (!is_known_visual_material_id(id)) {
        return kVisualMaterialDefinitions[0];
    }
    return kVisualMaterialDefinitions[static_cast<std::size_t>(id)];
}

[[nodiscard]] constexpr auto visual_material_definitions() noexcept
    -> std::span<const VisualMaterialDefinition> {
    return kVisualMaterialDefinitions;
}

[[nodiscard]] constexpr auto visual_material_for_block(BlockId block_id) noexcept -> VisualMaterialId {
    switch (static_cast<BlockType>(block_id)) {
    case BlockType::Grass:
        return VisualMaterialId::MeadowGrass;
    case BlockType::Dirt:
        return VisualMaterialId::Loam;
    case BlockType::Stone:
        return VisualMaterialId::WarmStone;
    case BlockType::Sand:
        return VisualMaterialId::SunlitSand;
    case BlockType::Wood:
        return VisualMaterialId::OakBark;
    case BlockType::Leaves:
        return VisualMaterialId::Broadleaf;
    case BlockType::Torch:
    case BlockType::TorchWallPositiveX:
    case BlockType::TorchWallNegativeX:
    case BlockType::TorchWallPositiveZ:
    case BlockType::TorchWallNegativeZ:
        return VisualMaterialId::TorchFlame;
    case BlockType::Cobblestone:
        return VisualMaterialId::Cobblestone;
    case BlockType::Planks:
        return VisualMaterialId::TerracottaPlanks;
    case BlockType::Gravel:
        return VisualMaterialId::RiverGravel;
    case BlockType::MossyStone:
        return VisualMaterialId::MossyStone;
    case BlockType::Snow:
        return VisualMaterialId::PowderSnow;
    case BlockType::PineWood:
        return VisualMaterialId::PineBark;
    case BlockType::PineLeaves:
        return VisualMaterialId::PineNeedles;
    case BlockType::TallGrass:
        return VisualMaterialId::TallGrass;
    case BlockType::RedFlower:
        return VisualMaterialId::CrimsonFlower;
    case BlockType::YellowFlower:
        return VisualMaterialId::GoldenFlower;
    case BlockType::DeadShrub:
        return VisualMaterialId::DeadShrub;
    case BlockType::Cactus:
        return VisualMaterialId::CactusSkin;
    case BlockType::Water:
        return VisualMaterialId::ClearWater;
    case BlockType::Glass:
        return VisualMaterialId::ClearGlass;
    case BlockType::Pastron:
        return VisualMaterialId::BronzeArmor;
    case BlockType::RoundShield:
        return VisualMaterialId::WoodShield;
    case BlockType::Sword:
    case BlockType::Spear:
        return VisualMaterialId::ForgedSteel;
    case BlockType::Shoes:
    case BlockType::Pants:
        return VisualMaterialId::Leather;
    case BlockType::CoalOre:
        return VisualMaterialId::CoalOre;
    case BlockType::IronOre:
        return VisualMaterialId::IronOre;
    case BlockType::GoldOre:
        return VisualMaterialId::GoldOre;
    case BlockType::DiamondOre:
        return VisualMaterialId::DiamondOre;
    case BlockType::MetallicAlloyOre:
        return VisualMaterialId::AlloyOre;
    case BlockType::Pickaxe:
    case BlockType::Axe:
    case BlockType::Shovel:
    case BlockType::Musket:
        return VisualMaterialId::ToolWoodSteel;
    case BlockType::Air:
    default:
        return VisualMaterialId::None;
    }
}

[[nodiscard]] constexpr auto visual_surface_for_block(BlockId block_id) noexcept
    -> VisualSurfaceClass {
    return visual_material_definition(visual_material_for_block(block_id)).surface_class;
}

[[nodiscard]] constexpr auto visual_material_name_hash(std::string_view name) noexcept -> std::uint32_t {
    auto hash = std::uint32_t {2166136261U};
    for (const char character : name) {
        hash ^= static_cast<std::uint8_t>(character);
        hash *= 16777619U;
    }
    return hash;
}

enum class VisualMaterialTexture : std::uint8_t {
    Albedo = 0,
    NormalHeight = 1,
    OrmEmission = 2,
};

inline constexpr std::array<std::uint8_t, 8> kVisualMaterialPackMagic {
    'V', 'C', 'V', 'M', 'A', 'T', '0', '1',
};
inline constexpr std::uint16_t kVisualMaterialPackVersion = 1U;
inline constexpr std::uint16_t kVisualMaterialPackHeaderSize = 48U;
inline constexpr std::uint8_t kVisualMaterialPackTextureCount = 3U;
inline constexpr std::uint8_t kVisualMaterialPackChannelCount = 4U;

struct VisualMaterialPackLayer {
    VisualMaterialId material_id = VisualMaterialId::None;
    VisualSurfaceClass surface_class = VisualSurfaceClass::Proxy;
    std::uint32_t name_hash = 0U;
};

struct VisualMaterialPack {
    std::uint16_t format_version = 0U;
    std::uint16_t width = 0U;
    std::uint16_t height = 0U;
    std::uint16_t mip_count = 0U;
    std::uint64_t content_checksum = 0U;
    std::vector<VisualMaterialPackLayer> layers {};
    std::vector<std::uint8_t> texels {};

    [[nodiscard]] auto texels_for(VisualMaterialId material_id,
                                  VisualMaterialTexture texture,
                                  std::uint16_t mip_level) const noexcept
        -> std::span<const std::uint8_t>;
};

enum class VisualMaterialPackError : std::uint8_t {
    None = 0,
    IoFailure,
    Truncated,
    InvalidMagic,
    UnsupportedVersion,
    InvalidHeader,
    InvalidDimensions,
    InvalidMipChain,
    InvalidLayerTable,
    SizeMismatch,
    ChecksumMismatch,
};

struct VisualMaterialPackLoadResult {
    std::optional<VisualMaterialPack> pack {};
    VisualMaterialPackError error = VisualMaterialPackError::None;
    std::string message {};

    [[nodiscard]] explicit operator bool() const noexcept {
        return pack.has_value() && error == VisualMaterialPackError::None;
    }
};

[[nodiscard]] auto visual_material_pack_checksum(std::span<const std::uint8_t> bytes) noexcept
    -> std::uint64_t;

[[nodiscard]] auto parse_visual_material_pack(std::span<const std::uint8_t> bytes)
    -> VisualMaterialPackLoadResult;

[[nodiscard]] auto load_visual_material_pack(const std::filesystem::path& path)
    -> VisualMaterialPackLoadResult;

} // namespace valcraft
