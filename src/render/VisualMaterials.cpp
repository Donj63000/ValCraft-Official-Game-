#include "render/VisualMaterials.h"

#include <algorithm>
#include <fstream>
#include <limits>
#include <system_error>
#include <utility>

namespace valcraft {
namespace {

// Je valide chaque pack contre le catalogue append-only declare dans le
// header afin qu'une couche ajoutee ne puisse jamais etre chargee de travers.

constexpr std::uint8_t kPackEncodingUnorm8 = 1U;
constexpr std::uint8_t kPackFlagCompleteMipChain = 1U;
constexpr std::uint32_t kPackLayerRecordSize = 8U;
constexpr std::uint64_t kMaximumPackFileSize = 512ULL * 1024ULL * 1024ULL;

[[nodiscard]] constexpr auto read_u16(std::span<const std::uint8_t> bytes,
                                      std::size_t offset) noexcept -> std::uint16_t {
    return static_cast<std::uint16_t>(
        static_cast<std::uint16_t>(bytes[offset]) |
        static_cast<std::uint16_t>(static_cast<std::uint16_t>(bytes[offset + 1U]) << 8U));
}

[[nodiscard]] constexpr auto read_u32(std::span<const std::uint8_t> bytes,
                                      std::size_t offset) noexcept -> std::uint32_t {
    auto value = std::uint32_t {0U};
    for (std::size_t byte_index = 0U; byte_index < 4U; ++byte_index) {
        value |= static_cast<std::uint32_t>(bytes[offset + byte_index])
                 << static_cast<unsigned int>(byte_index * 8U);
    }
    return value;
}

[[nodiscard]] constexpr auto read_u64(std::span<const std::uint8_t> bytes,
                                      std::size_t offset) noexcept -> std::uint64_t {
    auto value = std::uint64_t {0U};
    for (std::size_t byte_index = 0U; byte_index < 8U; ++byte_index) {
        value |= static_cast<std::uint64_t>(bytes[offset + byte_index])
                 << static_cast<unsigned int>(byte_index * 8U);
    }
    return value;
}

[[nodiscard]] constexpr auto is_power_of_two(std::uint16_t value) noexcept -> bool {
    return value != 0U && (value & static_cast<std::uint16_t>(value - 1U)) == 0U;
}

[[nodiscard]] constexpr auto complete_mip_count(std::uint16_t width,
                                                std::uint16_t height) noexcept -> std::uint16_t {
    auto dimension = std::max(width, height);
    auto count = std::uint16_t {1U};
    while (dimension > 1U) {
        dimension = static_cast<std::uint16_t>(dimension / 2U);
        ++count;
    }
    return count;
}

[[nodiscard]] constexpr auto mip_dimension(std::uint16_t dimension,
                                           std::uint16_t mip_level) noexcept -> std::size_t {
    auto value = static_cast<std::size_t>(dimension);
    for (std::uint16_t level = 0U; level < mip_level && value > 1U; ++level) {
        value /= 2U;
    }
    return std::max<std::size_t>(value, 1U);
}

[[nodiscard]] auto bytes_per_texture(std::uint16_t width,
                                     std::uint16_t height,
                                     std::uint16_t mip_count) noexcept
    -> std::optional<std::size_t> {
    auto total = std::size_t {0U};
    for (std::uint16_t mip_level = 0U; mip_level < mip_count; ++mip_level) {
        const auto mip_width = mip_dimension(width, mip_level);
        const auto mip_height = mip_dimension(height, mip_level);
        if (mip_width > (std::numeric_limits<std::size_t>::max)() / mip_height) {
            return std::nullopt;
        }
        const auto pixel_count = mip_width * mip_height;
        if (pixel_count > (std::numeric_limits<std::size_t>::max)() / kVisualMaterialPackChannelCount) {
            return std::nullopt;
        }
        const auto byte_count = pixel_count * kVisualMaterialPackChannelCount;
        if (total > (std::numeric_limits<std::size_t>::max)() - byte_count) {
            return std::nullopt;
        }
        total += byte_count;
    }
    return total;
}

[[nodiscard]] auto failure(VisualMaterialPackError error, std::string message)
    -> VisualMaterialPackLoadResult {
    VisualMaterialPackLoadResult result {};
    result.error = error;
    result.message = std::move(message);
    return result;
}

} // namespace

auto visual_material_pack_checksum(std::span<const std::uint8_t> bytes) noexcept
    -> std::uint64_t {
    // Je calcule le checksum sur la table des couches et les texels afin que
    // toute corruption, y compris sémantique, soit détectée avant un upload GPU.
    auto checksum = std::uint64_t {14695981039346656037ULL};
    for (const auto byte : bytes) {
        checksum ^= static_cast<std::uint64_t>(byte);
        checksum *= 1099511628211ULL;
    }
    return checksum;
}

auto VisualMaterialPack::texels_for(VisualMaterialId material_id,
                                    VisualMaterialTexture texture,
                                    std::uint16_t mip_level) const noexcept
    -> std::span<const std::uint8_t> {
    const auto texture_index = static_cast<std::uint8_t>(texture);
    if (texture_index >= kVisualMaterialPackTextureCount || mip_level >= mip_count) {
        return {};
    }

    auto layer_index = layers.size();
    for (std::size_t index = 0U; index < layers.size(); ++index) {
        if (layers[index].material_id == material_id) {
            layer_index = index;
            break;
        }
    }
    if (layer_index == layers.size()) {
        return {};
    }

    const auto semantic_stride = bytes_per_texture(width, height, mip_count);
    if (!semantic_stride.has_value()) {
        return {};
    }

    auto mip_offset = std::size_t {0U};
    for (std::uint16_t level = 0U; level < mip_level; ++level) {
        mip_offset += mip_dimension(width, level) *
                      mip_dimension(height, level) *
                      kVisualMaterialPackChannelCount;
    }
    const auto mip_byte_count = mip_dimension(width, mip_level) *
                                mip_dimension(height, mip_level) *
                                kVisualMaterialPackChannelCount;
    const auto layer_stride = *semantic_stride * kVisualMaterialPackTextureCount;
    const auto offset = layer_index * layer_stride +
                        static_cast<std::size_t>(texture_index) * *semantic_stride +
                        mip_offset;
    if (offset > texels.size() || mip_byte_count > texels.size() - offset) {
        return {};
    }
    return std::span<const std::uint8_t>(texels).subspan(offset, mip_byte_count);
}

auto parse_visual_material_pack(std::span<const std::uint8_t> bytes)
    -> VisualMaterialPackLoadResult {
    if (bytes.size() < kVisualMaterialPackHeaderSize) {
        return failure(VisualMaterialPackError::Truncated,
                       "Le pack de matériaux est plus court que son en-tête.");
    }

    if (!std::equal(kVisualMaterialPackMagic.begin(),
                    kVisualMaterialPackMagic.end(),
                    bytes.begin())) {
        return failure(VisualMaterialPackError::InvalidMagic,
                       "La signature du pack de matériaux est invalide.");
    }

    const auto format_version = read_u16(bytes, 8U);
    if (format_version != kVisualMaterialPackVersion) {
        return failure(VisualMaterialPackError::UnsupportedVersion,
                       "La version du pack de matériaux n'est pas prise en charge.");
    }

    const auto header_size = read_u16(bytes, 10U);
    const auto width = read_u16(bytes, 12U);
    const auto height = read_u16(bytes, 14U);
    const auto layer_count = read_u16(bytes, 16U);
    const auto mip_count = read_u16(bytes, 18U);
    const auto texture_count = bytes[20U];
    const auto channel_count = bytes[21U];
    const auto encoding = bytes[22U];
    const auto flags = bytes[23U];
    const auto layer_record_size = read_u32(bytes, 24U);
    const auto layer_table_byte_count = read_u32(bytes, 28U);
    const auto declared_payload_byte_count = read_u64(bytes, 32U);
    const auto declared_checksum = read_u64(bytes, 40U);

    if (header_size != kVisualMaterialPackHeaderSize ||
        texture_count != kVisualMaterialPackTextureCount ||
        channel_count != kVisualMaterialPackChannelCount ||
        encoding != kPackEncodingUnorm8 ||
        flags != kPackFlagCompleteMipChain ||
        layer_record_size != kPackLayerRecordSize) {
        return failure(VisualMaterialPackError::InvalidHeader,
                       "Les caractéristiques de l'en-tête du pack sont invalides.");
    }

    if (width < 16U || height < 16U || width > 4096U || height > 4096U ||
        !is_power_of_two(width) || !is_power_of_two(height)) {
        return failure(VisualMaterialPackError::InvalidDimensions,
                       "Les dimensions du pack doivent être des puissances de deux comprises entre 16 et 4096.");
    }

    if (mip_count != complete_mip_count(width, height)) {
        return failure(VisualMaterialPackError::InvalidMipChain,
                       "Le pack ne contient pas une chaîne complète de mipmaps.");
    }

    constexpr auto expected_layer_count =
        static_cast<std::uint16_t>(kVisualMaterialCount - 1U);
    if (layer_count != expected_layer_count ||
        layer_table_byte_count != static_cast<std::uint32_t>(layer_count) * kPackLayerRecordSize) {
        return failure(VisualMaterialPackError::InvalidLayerTable,
                       "La table des couches ne correspond pas au catalogue visuel.");
    }

    const auto texture_byte_count = bytes_per_texture(width, height, mip_count);
    if (!texture_byte_count.has_value()) {
        return failure(VisualMaterialPackError::SizeMismatch,
                       "La taille calculée du pack dépasse les limites de la plateforme.");
    }
    const auto expected_payload_byte_count =
        static_cast<std::uint64_t>(*texture_byte_count) *
        static_cast<std::uint64_t>(texture_count) *
        static_cast<std::uint64_t>(layer_count);
    if (declared_payload_byte_count != expected_payload_byte_count ||
        declared_payload_byte_count > kMaximumPackFileSize) {
        return failure(VisualMaterialPackError::SizeMismatch,
                       "La taille déclarée des texels est incohérente.");
    }

    const auto content_offset = static_cast<std::size_t>(header_size);
    const auto table_size = static_cast<std::size_t>(layer_table_byte_count);
    const auto payload_size = static_cast<std::size_t>(declared_payload_byte_count);
    if (table_size > (std::numeric_limits<std::size_t>::max)() - content_offset ||
        payload_size > (std::numeric_limits<std::size_t>::max)() - content_offset - table_size) {
        return failure(VisualMaterialPackError::SizeMismatch,
                       "La taille totale du pack dépasse les limites de la plateforme.");
    }
    const auto expected_file_size = content_offset + table_size + payload_size;
    if (bytes.size() != expected_file_size) {
        return failure(VisualMaterialPackError::SizeMismatch,
                       "La taille réelle du pack ne correspond pas à son en-tête.");
    }

    if (visual_material_pack_checksum(bytes.subspan(content_offset)) != declared_checksum) {
        return failure(VisualMaterialPackError::ChecksumMismatch,
                       "Le checksum du pack de matériaux est invalide.");
    }

    VisualMaterialPack pack {};
    pack.format_version = format_version;
    pack.width = width;
    pack.height = height;
    pack.mip_count = mip_count;
    pack.content_checksum = declared_checksum;
    pack.layers.reserve(layer_count);

    for (std::uint16_t layer_index = 0U; layer_index < layer_count; ++layer_index) {
        const auto record_offset = content_offset +
                                   static_cast<std::size_t>(layer_index) * kPackLayerRecordSize;
        const auto material_id = static_cast<VisualMaterialId>(read_u16(bytes, record_offset));
        const auto surface_class = static_cast<VisualSurfaceClass>(bytes[record_offset + 2U]);
        const auto reserved = bytes[record_offset + 3U];
        const auto name_hash = read_u32(bytes, record_offset + 4U);
        const auto expected_id = static_cast<VisualMaterialId>(layer_index + 1U);
        const auto& definition = visual_material_definition(expected_id);

        if (material_id != expected_id ||
            definition.pack_layer != layer_index ||
            surface_class != definition.surface_class ||
            reserved != 0U ||
            name_hash != visual_material_name_hash(definition.name)) {
            return failure(VisualMaterialPackError::InvalidLayerTable,
                           "Une couche du pack ne correspond pas à sa définition C++.");
        }
        pack.layers.push_back({material_id, surface_class, name_hash});
    }

    const auto payload_offset = content_offset + table_size;
    const auto payload = bytes.subspan(payload_offset, payload_size);
    pack.texels.assign(payload.begin(), payload.end());

    VisualMaterialPackLoadResult result {};
    result.pack.emplace(std::move(pack));
    return result;
}

auto load_visual_material_pack(const std::filesystem::path& path)
    -> VisualMaterialPackLoadResult {
    std::error_code file_error;
    const auto file_size = std::filesystem::file_size(path, file_error);
    if (file_error) {
        return failure(VisualMaterialPackError::IoFailure,
                       "Impossible de lire la taille du pack de matériaux : " + file_error.message());
    }
    if (file_size > kMaximumPackFileSize ||
        file_size > static_cast<std::uintmax_t>((std::numeric_limits<std::size_t>::max)())) {
        return failure(VisualMaterialPackError::SizeMismatch,
                       "Le pack de matériaux dépasse la taille maximale autorisée.");
    }

    std::ifstream input(path, std::ios::binary);
    if (!input) {
        return failure(VisualMaterialPackError::IoFailure,
                       "Impossible d'ouvrir le pack de matériaux.");
    }

    std::vector<std::uint8_t> bytes(static_cast<std::size_t>(file_size));
    if (!bytes.empty()) {
        input.read(reinterpret_cast<char*>(bytes.data()),
                   static_cast<std::streamsize>(bytes.size()));
        if (!input || input.gcount() != static_cast<std::streamsize>(bytes.size())) {
            return failure(VisualMaterialPackError::IoFailure,
                           "Le pack de matériaux n'a pas pu être lu entièrement.");
        }
    }
    return parse_visual_material_pack(bytes);
}

} // namespace valcraft
