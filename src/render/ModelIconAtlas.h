#pragma once

#include "render/VisualItemModel.h"
#include "render/VisualMaterials.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace valcraft {

inline constexpr std::array<std::uint8_t, 8> kModelIconAtlasMagic {
    'V', 'C', 'I', 'C', 'O', 'N', '0', '1',
};
inline constexpr std::uint16_t kModelIconAtlasVersion = 1U;
// Je change la recette lorsque le cadrage ou le catalogue visuel evolue afin
// qu'un ancien atlas ne puisse jamais etre accepte silencieusement.
inline constexpr std::uint32_t kModelIconRecipeVersion = 2U;
inline constexpr std::uint16_t kModelIconAtlasHeaderSize = 64U;
inline constexpr std::uint32_t kModelIconAtlasLayerRecordSize = 32U;
inline constexpr std::uint16_t kModelIconSize = 128U;
inline constexpr std::uint16_t kModelIconMipCount = 8U;
inline constexpr std::uint8_t kModelIconChannelCount = 4U;

enum class ModelIconLayerFlags : std::uint16_t {
    None = 0U,
    AlphaMaterial = 1U << 0U,
    TwoSided = 1U << 1U,
    Emissive = 1U << 2U,
};

[[nodiscard]] constexpr auto operator|(
    ModelIconLayerFlags lhs,
    ModelIconLayerFlags rhs) noexcept -> ModelIconLayerFlags {
    return static_cast<ModelIconLayerFlags>(
        static_cast<std::uint16_t>(lhs) |
        static_cast<std::uint16_t>(rhs));
}

[[nodiscard]] constexpr auto has_model_icon_layer_flag(
    ModelIconLayerFlags flags,
    ModelIconLayerFlags expected) noexcept -> bool {
    return (static_cast<std::uint16_t>(flags) &
            static_cast<std::uint16_t>(expected)) != 0U;
}

struct ModelIconMipLevel {
    std::uint16_t width = 0U;
    std::uint16_t height = 0U;
    std::size_t offset_within_layer = 0U;
    std::size_t byte_count = 0U;

    auto operator==(const ModelIconMipLevel&) const -> bool = default;
};

struct ModelIconLayer {
    BlockId item_id = to_block_id(BlockType::Air);
    VisualItemModelClass model_class =
        VisualItemModelClass::OrganicBlock;
    VisualMaterialId primary_material = VisualMaterialId::None;
    std::uint16_t primitive_count = 0U;
    ModelIconLayerFlags flags = ModelIconLayerFlags::None;
    std::uint64_t geometry_checksum = 0U;
    std::uint64_t texel_checksum = 0U;

    auto operator==(const ModelIconLayer&) const -> bool = default;
};

struct ModelIconAtlasMetadata {
    std::uint16_t version = 0U;
    std::uint16_t width = 0U;
    std::uint16_t height = 0U;
    std::uint16_t mip_count = 0U;
    std::uint32_t recipe_version = 0U;
    std::uint64_t source_material_checksum = 0U;
    std::uint64_t content_checksum = 0U;

    auto operator==(const ModelIconAtlasMetadata&) const -> bool = default;
};

struct ModelIconAtlas {
    ModelIconAtlasMetadata metadata {};
    std::vector<ModelIconLayer> layers {};
    std::vector<ModelIconMipLevel> mip_levels {};
    std::vector<std::uint8_t> texels {};

    [[nodiscard]] auto layer_for(
        BlockId block_id) const noexcept -> const ModelIconLayer*;

    [[nodiscard]] auto texels_for(
        BlockId block_id,
        std::uint16_t mip_level) const noexcept
        -> std::span<const std::uint8_t>;
};

enum class ModelIconAtlasError : std::uint8_t {
    None = 0,
    IoFailure,
    InvalidMaterialPack,
    InvalidModel,
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

struct ModelIconAtlasResult {
    std::optional<ModelIconAtlas> atlas {};
    ModelIconAtlasError error = ModelIconAtlasError::None;
    std::string message {};

    [[nodiscard]] explicit operator bool() const noexcept {
        return atlas.has_value() &&
               error == ModelIconAtlasError::None;
    }
};

[[nodiscard]] auto model_icon_atlas_checksum(
    std::span<const std::uint8_t> bytes) noexcept -> std::uint64_t;

// Je rasterise hors ligne les modeles partages avec une projection
// orthographique stable. Aucun contexte OpenGL n'est necessaire.
[[nodiscard]] auto generate_model_icon_atlas(
    const VisualMaterialPack& material_pack) -> ModelIconAtlasResult;

[[nodiscard]] auto serialize_model_icon_atlas(
    const ModelIconAtlas& atlas) -> std::vector<std::uint8_t>;

[[nodiscard]] auto parse_model_icon_atlas(
    std::span<const std::uint8_t> bytes) -> ModelIconAtlasResult;

[[nodiscard]] auto load_model_icon_atlas(
    const std::filesystem::path& path) -> ModelIconAtlasResult;

[[nodiscard]] auto write_model_icon_atlas(
    const std::filesystem::path& path,
    const ModelIconAtlas& atlas,
    std::string* error_message = nullptr) -> bool;

} // namespace valcraft
