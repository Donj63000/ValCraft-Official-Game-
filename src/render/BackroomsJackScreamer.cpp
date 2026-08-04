#include "render/BackroomsJackScreamer.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <fstream>
#include <limits>
#include <new>
#include <span>
#include <string_view>
#include <system_error>

namespace valcraft {

namespace {

constexpr auto kMaximumScreamerDimension = 8'192;
constexpr auto kBitmapFileHeaderBytes = 14U;
constexpr auto kBitmapInfoHeaderBytes = 40U;
constexpr std::uint64_t kMebibyte = UINT64_C(1'048'576);
constexpr std::uint64_t kMaximumDecodedImageBytes =
    UINT64_C(64) * kMebibyte;
constexpr std::uint64_t kMaximumBitmapMetadataBytes =
    UINT64_C(1) * kMebibyte;
constexpr std::uint64_t kMaximumScreamerFileBytes =
    kMaximumDecodedImageBytes + kMaximumBitmapMetadataBytes;

[[nodiscard]] auto read_u16(
    std::span<const std::uint8_t> bytes,
    std::size_t offset) noexcept -> std::uint16_t {
    if (offset > bytes.size() || bytes.size() - offset < 2U) {
        return 0U;
    }
    return static_cast<std::uint16_t>(bytes[offset]) |
           static_cast<std::uint16_t>(
               static_cast<std::uint16_t>(bytes[offset + 1U]) << 8U);
}

[[nodiscard]] auto read_u32(
    std::span<const std::uint8_t> bytes,
    std::size_t offset) noexcept -> std::uint32_t {
    if (offset > bytes.size() || bytes.size() - offset < 4U) {
        return 0U;
    }
    return static_cast<std::uint32_t>(bytes[offset]) |
           (static_cast<std::uint32_t>(bytes[offset + 1U]) << 8U) |
           (static_cast<std::uint32_t>(bytes[offset + 2U]) << 16U) |
           (static_cast<std::uint32_t>(bytes[offset + 3U]) << 24U);
}

[[nodiscard]] auto read_i32(
    std::span<const std::uint8_t> bytes,
    std::size_t offset) noexcept -> std::int32_t {
    return static_cast<std::int32_t>(read_u32(bytes, offset));
}

[[nodiscard]] auto checked_add(
    std::uint64_t left,
    std::uint64_t right,
    std::uint64_t& result) noexcept -> bool {

    if (left >
        std::numeric_limits<std::uint64_t>::max() - right) {
        return false;
    }
    result = left + right;
    return true;
}

[[nodiscard]] auto checked_multiply(
    std::uint64_t left,
    std::uint64_t right,
    std::uint64_t& result) noexcept -> bool {

    if (left != 0U &&
        right >
            std::numeric_limits<std::uint64_t>::max() / left) {
        return false;
    }
    result = left * right;
    return true;
}

[[nodiscard]] auto failure(std::string message)
    -> BackroomsJackScreamerImage {
    BackroomsJackScreamerImage image {};
    image.error = std::move(message);
    return image;
}

[[nodiscard]] auto load_backrooms_screamer_bmp(
    const std::filesystem::path& path,
    std::string_view subject) -> BackroomsJackScreamerImage {
    const auto owner = std::string {subject};
    std::ifstream stream(path, std::ios::binary | std::ios::ate);
    if (!stream) {
        return failure(
            "Impossible d'ouvrir l'image de screamer de " + owner + " : " +
            path.string());
    }

    const auto file_position = stream.tellg();
    if (file_position < 0) {
        return failure("Taille BMP invalide pour le screamer de " + owner);
    }
    const auto file_size =
        static_cast<std::uint64_t>(file_position);
    if (file_size <
        static_cast<std::uint64_t>(
            kBitmapFileHeaderBytes + kBitmapInfoHeaderBytes)) {
        return failure("BMP de " + owner + " tronque avant son en-tete");
    }
    // Je refuse la ressource avant toute allocation proportionnelle au fichier.
    // Le budget laisse 64 Mio de pixels décodés et 1 Mio de métadonnées BMP.
    if (file_size > kMaximumScreamerFileBytes) {
        return failure(
            "Le fichier BMP de " + owner +
            " depasse la limite de securite");
    }

    std::array<std::uint8_t,
               kBitmapFileHeaderBytes + kBitmapInfoHeaderBytes> header {};
    stream.seekg(0, std::ios::beg);
    if (!stream.read(
            reinterpret_cast<char*>(header.data()),
            static_cast<std::streamsize>(header.size()))) {
        return failure("Lecture incomplete de l'en-tete BMP de " + owner);
    }
    const auto bytes = std::span<const std::uint8_t> {header};

    if (bytes[0] != static_cast<std::uint8_t>('B') ||
        bytes[1] != static_cast<std::uint8_t>('M')) {
        return failure(
            "La ressource du screamer de " + owner + " n'est pas un BMP");
    }
    const auto declared_file_size = read_u32(bytes, 2U);
    const auto pixel_offset =
        static_cast<std::uint64_t>(read_u32(bytes, 10U));
    const auto dib_size = read_u32(bytes, 14U);
    const auto width_signed = read_i32(bytes, 18U);
    const auto height_signed = read_i32(bytes, 22U);
    const auto planes = read_u16(bytes, 26U);
    const auto bits_per_pixel = read_u16(bytes, 28U);
    const auto compression = read_u32(bytes, 30U);
    if (dib_size < kBitmapInfoHeaderBytes || planes != 1U ||
        (bits_per_pixel != 24U && bits_per_pixel != 32U) ||
        compression != 0U) {
        return failure(
            "Format BMP non pris en charge pour le screamer de " + owner);
    }
    if (width_signed <= 0 || height_signed == 0 ||
        height_signed == std::numeric_limits<std::int32_t>::lowest()) {
        return failure(
            "Dimensions BMP invalides pour le screamer de " + owner);
    }

    const auto width = static_cast<std::uint32_t>(width_signed);
    const auto height = static_cast<std::uint32_t>(
        height_signed < 0 ? -height_signed : height_signed);
    if (width > kMaximumScreamerDimension ||
        height > kMaximumScreamerDimension) {
        return failure(
            "Le BMP de " + owner + " depasse la limite de securite");
    }

    const auto bytes_per_pixel =
        static_cast<std::uint64_t>(bits_per_pixel / 8U);
    std::uint64_t row_bytes = 0U;
    std::uint64_t padded_row_bytes = 0U;
    if (!checked_multiply(width, bytes_per_pixel, row_bytes) ||
        !checked_add(row_bytes, 3U, padded_row_bytes)) {
        return failure("Debordement de ligne dans le BMP de " + owner);
    }
    const auto row_stride =
        padded_row_bytes & ~UINT64_C(3);
    std::uint64_t pixel_data_bytes = 0U;
    std::uint64_t required_bytes = 0U;
    if (!checked_multiply(row_stride, height, pixel_data_bytes) ||
        !checked_add(
            pixel_offset,
            pixel_data_bytes,
            required_bytes)) {
        return failure("Debordement de pixels dans le BMP de " + owner);
    }

    std::uint64_t minimum_pixel_offset = 0U;
    if (!checked_add(
            kBitmapFileHeaderBytes,
            dib_size,
            minimum_pixel_offset) ||
        minimum_pixel_offset > kMaximumBitmapMetadataBytes ||
        pixel_offset < minimum_pixel_offset ||
        pixel_offset > kMaximumBitmapMetadataBytes) {
        return failure(
            "En-tete BMP trop grand pour le screamer de " + owner);
    }
    if (required_bytes > file_size) {
        return failure("Pixels BMP tronques pour le screamer de " + owner);
    }

    if (declared_file_size != 0U &&
        (static_cast<std::uint64_t>(declared_file_size) < required_bytes ||
         static_cast<std::uint64_t>(declared_file_size) > file_size)) {
        return failure(
            "Taille declaree incoherente dans le BMP de " + owner);
    }

    std::uint64_t pixel_count = 0U;
    std::uint64_t decoded_bytes = 0U;
    if (!checked_multiply(width, height, pixel_count) ||
        !checked_multiply(pixel_count, 4U, decoded_bytes) ||
        decoded_bytes > kMaximumDecodedImageBytes ||
        decoded_bytes >
            static_cast<std::uint64_t>(
                std::numeric_limits<std::size_t>::max())) {
        return failure("Image BMP de " + owner + " trop grande");
    }
    if (row_stride >
            static_cast<std::uint64_t>(
                std::numeric_limits<std::size_t>::max()) ||
        row_stride >
            static_cast<std::uint64_t>(
                std::numeric_limits<std::streamsize>::max()) ||
        pixel_offset >
            static_cast<std::uint64_t>(
                std::numeric_limits<std::streamoff>::max())) {
        return failure("Taille BMP non representable pour le screamer de " + owner);
    }

    BackroomsJackScreamerImage image {};
    image.width = static_cast<int>(width);
    image.height = static_cast<int>(height);
    std::vector<std::uint8_t> source_row {};
    try {
        image.rgba.resize(
            static_cast<std::size_t>(decoded_bytes));
        source_row.resize(
            static_cast<std::size_t>(row_stride));
    } catch (const std::bad_alloc&) {
        return failure(
            "Memoire insuffisante pour decoder le BMP de " + owner);
    }

    stream.seekg(
        static_cast<std::streamoff>(pixel_offset),
        std::ios::beg);
    if (!stream) {
        return failure("Offset de pixels BMP invalide pour " + owner);
    }

    const auto width_size = static_cast<std::size_t>(width);
    const auto height_size = static_cast<std::size_t>(height);
    const auto bytes_per_pixel_size =
        static_cast<std::size_t>(bytes_per_pixel);
    const auto source_is_top_down = height_signed < 0;
    for (std::size_t source_y = 0U;
         source_y < height_size;
         ++source_y) {
        if (!stream.read(
                reinterpret_cast<char*>(source_row.data()),
                static_cast<std::streamsize>(source_row.size()))) {
            return failure("Pixels BMP tronques pour le screamer de " + owner);
        }
        const auto destination_y =
            source_is_top_down
                ? source_y
                : height_size - 1U - source_y;
        auto* destination_row =
            image.rgba.data() + destination_y * width_size * 4U;
        for (std::size_t x = 0U; x < width_size; ++x) {
            const auto* source =
                source_row.data() + x * bytes_per_pixel_size;
            auto* destination = destination_row + x * 4U;
            destination[0] = source[2];
            destination[1] = source[1];
            destination[2] = source[0];
            // Je force l'opacite : certains encodeurs BMP laissent l'octet
            // reserve a zero meme quand l'image est parfaitement valide.
            destination[3] = 255U;
        }
    }
    return image;
}

} // namespace

auto load_backrooms_jack_screamer_bmp(
    const std::filesystem::path& path) -> BackroomsJackScreamerImage {
    return load_backrooms_screamer_bmp(path, "Jack");
}

auto load_backrooms_marlow_screamer_bmp(
    const std::filesystem::path& path) -> BackroomsJackScreamerImage {
    return load_backrooms_screamer_bmp(path, "Marlow");
}

auto resolve_backrooms_jack_screamer_path(
    const std::filesystem::path& working_directory)
    -> std::filesystem::path {
    const std::array candidates {
        working_directory / "assets" / "backrooms" /
            "jack_le_pirate_screamer.bmp",
        working_directory / "bin" / "assets" / "backrooms" /
            "jack_le_pirate_screamer.bmp",
        working_directory.parent_path() / "assets" / "backrooms" /
            "jack_le_pirate_screamer.bmp",
        working_directory.parent_path().parent_path() / "assets" /
            "backrooms" / "jack_le_pirate_screamer.bmp",
    };
    std::error_code error;
    for (const auto& candidate : candidates) {
        if (std::filesystem::is_regular_file(candidate, error)) {
            return candidate;
        }
        error.clear();
    }
    return {};
}

auto resolve_backrooms_marlow_screamer_path(
    const std::filesystem::path& working_directory)
    -> std::filesystem::path {
    const std::array candidates {
        working_directory / "assets" / "backrooms" /
            "marlow_le_noye_screamer.bmp",
        working_directory / "bin" / "assets" / "backrooms" /
            "marlow_le_noye_screamer.bmp",
        working_directory.parent_path() / "assets" / "backrooms" /
            "marlow_le_noye_screamer.bmp",
        working_directory.parent_path().parent_path() / "assets" /
            "backrooms" / "marlow_le_noye_screamer.bmp",
    };
    std::error_code error;
    for (const auto& candidate : candidates) {
        if (std::filesystem::is_regular_file(candidate, error)) {
            return candidate;
        }
        error.clear();
    }
    return {};
}

} // namespace valcraft
