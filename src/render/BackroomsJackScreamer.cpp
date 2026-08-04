#include "render/BackroomsJackScreamer.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <fstream>
#include <limits>
#include <string_view>
#include <system_error>

namespace valcraft {

namespace {

constexpr auto kMaximumScreamerDimension = 8'192;
constexpr auto kBitmapFileHeaderBytes = 14U;
constexpr auto kBitmapInfoHeaderBytes = 40U;

[[nodiscard]] auto read_u16(
    const std::vector<std::uint8_t>& bytes,
    std::size_t offset) noexcept -> std::uint16_t {
    if (offset > bytes.size() || bytes.size() - offset < 2U) {
        return 0U;
    }
    return static_cast<std::uint16_t>(bytes[offset]) |
           static_cast<std::uint16_t>(
               static_cast<std::uint16_t>(bytes[offset + 1U]) << 8U);
}

[[nodiscard]] auto read_u32(
    const std::vector<std::uint8_t>& bytes,
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
    const std::vector<std::uint8_t>& bytes,
    std::size_t offset) noexcept -> std::int32_t {
    return static_cast<std::int32_t>(read_u32(bytes, offset));
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

    const auto file_size = stream.tellg();
    if (file_size < 0 ||
        static_cast<std::uint64_t>(file_size) >
            static_cast<std::uint64_t>(
                std::numeric_limits<std::size_t>::max())) {
        return failure("Taille BMP invalide pour le screamer de " + owner);
    }
    const auto byte_count = static_cast<std::size_t>(file_size);
    if (byte_count < kBitmapFileHeaderBytes + kBitmapInfoHeaderBytes) {
        return failure("BMP de " + owner + " tronque avant son en-tete");
    }

    std::vector<std::uint8_t> bytes(byte_count);
    stream.seekg(0, std::ios::beg);
    if (!stream.read(
            reinterpret_cast<char*>(bytes.data()),
            static_cast<std::streamsize>(bytes.size()))) {
        return failure("Lecture incomplete du BMP de " + owner);
    }

    if (bytes[0] != static_cast<std::uint8_t>('B') ||
        bytes[1] != static_cast<std::uint8_t>('M')) {
        return failure(
            "La ressource du screamer de " + owner + " n'est pas un BMP");
    }
    const auto pixel_offset =
        static_cast<std::size_t>(read_u32(bytes, 10U));
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
        static_cast<std::size_t>(bits_per_pixel / 8U);
    const auto width_size = static_cast<std::size_t>(width);
    const auto height_size = static_cast<std::size_t>(height);
    if (width_size >
        (std::numeric_limits<std::size_t>::max() - 3U) /
            bytes_per_pixel) {
        return failure("Debordement de ligne dans le BMP de " + owner);
    }
    const auto row_stride =
        (width_size * bytes_per_pixel + 3U) & ~std::size_t{3U};
    if (height_size != 0U &&
        row_stride >
            (std::numeric_limits<std::size_t>::max() - pixel_offset) /
                height_size) {
        return failure("Debordement de pixels dans le BMP de " + owner);
    }
    const auto required_bytes = pixel_offset + row_stride * height_size;
    if (pixel_offset < kBitmapFileHeaderBytes + dib_size ||
        required_bytes > bytes.size()) {
        return failure("Pixels BMP tronques pour le screamer de " + owner);
    }
    if (width_size >
        std::numeric_limits<std::size_t>::max() /
            std::max<std::size_t>(height_size * 4U, 1U)) {
        return failure("Image BMP de " + owner + " trop grande");
    }

    BackroomsJackScreamerImage image {};
    image.width = static_cast<int>(width);
    image.height = static_cast<int>(height);
    image.rgba.resize(width_size * height_size * 4U);
    const auto source_is_top_down = height_signed < 0;
    for (std::size_t destination_y = 0U;
         destination_y < height_size;
         ++destination_y) {
        const auto source_y =
            source_is_top_down
                ? destination_y
                : height_size - 1U - destination_y;
        const auto* source_row =
            bytes.data() + pixel_offset + source_y * row_stride;
        auto* destination_row =
            image.rgba.data() + destination_y * width_size * 4U;
        for (std::size_t x = 0U; x < width_size; ++x) {
            const auto* source = source_row + x * bytes_per_pixel;
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
