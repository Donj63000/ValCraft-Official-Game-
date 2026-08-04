#include "render/BackroomsJackScreamer.h"

#include <doctest/doctest.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <limits>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

namespace valcraft {

namespace {

constexpr std::size_t kTestBitmapHeaderBytes = 54U;

struct TemporaryBmpFile {
    std::filesystem::path directory {};
    std::filesystem::path path {};

    explicit TemporaryBmpFile(std::string_view stem) {
        static std::atomic<std::uint64_t> sequence {0U};
        const auto timestamp = static_cast<std::uint64_t>(
            std::chrono::steady_clock::now()
                .time_since_epoch()
                .count());
        for (std::uint64_t attempt = 0U; attempt < 32U; ++attempt) {
            const auto ticket = sequence.fetch_add(1U);
            const auto candidate =
                std::filesystem::temp_directory_path() /
                ("valcraft_" + std::string(stem) + "_" +
                 std::to_string(timestamp) + "_" +
                 std::to_string(ticket) + "_" +
                 std::to_string(attempt));
            std::error_code error;
            if (std::filesystem::create_directory(candidate, error)) {
                directory = candidate;
                path = directory / "fixture.bmp";
                return;
            }
            if (error) {
                throw std::filesystem::filesystem_error(
                    "Je ne peux pas creer le repertoire BMP temporaire",
                    candidate,
                    error);
            }
        }
        throw std::runtime_error(
            "Je ne peux pas attribuer un nom BMP temporaire unique");
    }

    ~TemporaryBmpFile() {
        std::error_code ignored;
        std::filesystem::remove_all(directory, ignored);
    }
};

void write_u16(
    std::vector<std::uint8_t>& bytes,
    std::size_t offset,
    std::uint16_t value) {

    bytes[offset] = static_cast<std::uint8_t>(value & 0xFFU);
    bytes[offset + 1U] =
        static_cast<std::uint8_t>((value >> 8U) & 0xFFU);
}

void write_u32(
    std::vector<std::uint8_t>& bytes,
    std::size_t offset,
    std::uint32_t value) {

    for (std::size_t byte = 0U; byte < 4U; ++byte) {
        bytes[offset + byte] =
            static_cast<std::uint8_t>(
                (value >> (byte * 8U)) & 0xFFU);
    }
}

auto make_test_bitmap(
    std::int32_t width,
    std::int32_t height,
    std::uint16_t bits_per_pixel) -> std::vector<std::uint8_t> {

    const auto signed_height = static_cast<std::int64_t>(height);
    const auto absolute_height = static_cast<std::uint32_t>(
        signed_height < 0 ? -signed_height : signed_height);
    const auto bytes_per_pixel =
        static_cast<std::size_t>(bits_per_pixel / 8U);
    const auto row_stride =
        (static_cast<std::size_t>(width) *
             bytes_per_pixel +
         3U) &
        ~std::size_t {3U};
    auto bytes = std::vector<std::uint8_t>(
        kTestBitmapHeaderBytes +
            row_stride * absolute_height,
        0U);
    bytes[0] = static_cast<std::uint8_t>('B');
    bytes[1] = static_cast<std::uint8_t>('M');
    write_u32(
        bytes,
        2U,
        static_cast<std::uint32_t>(bytes.size()));
    write_u32(
        bytes,
        10U,
        static_cast<std::uint32_t>(kTestBitmapHeaderBytes));
    write_u32(bytes, 14U, 40U);
    write_u32(bytes, 18U, static_cast<std::uint32_t>(width));
    write_u32(bytes, 22U, static_cast<std::uint32_t>(height));
    write_u16(bytes, 26U, 1U);
    write_u16(bytes, 28U, bits_per_pixel);
    write_u32(
        bytes,
        34U,
        static_cast<std::uint32_t>(
            row_stride * absolute_height));
    return bytes;
}

void write_bytes(
    const std::filesystem::path& path,
    const std::vector<std::uint8_t>& bytes) {

    std::ofstream output(
        path,
        std::ios::binary | std::ios::trunc);
    REQUIRE(output.is_open());
    output.write(
        reinterpret_cast<const char*>(bytes.data()),
        static_cast<std::streamsize>(bytes.size()));
    REQUIRE(output.good());
}

} // namespace

TEST_CASE("Jack screamer asset resolves and decodes to opaque RGBA") {
    const auto path =
        resolve_backrooms_jack_screamer_path(
            std::filesystem::current_path());
    REQUIRE_FALSE(path.empty());

    const auto image =
        load_backrooms_jack_screamer_bmp(path);
    INFO(image.error);
    REQUIRE(image.valid());
    CHECK(image.width == 1280);
    CHECK(image.height == 720);
    CHECK(image.rgba.size() == 1280U * 720U * 4U);

    for (std::size_t index = 3U;
         index < image.rgba.size();
         index += 4U) {
        REQUIRE(image.rgba[index] == 255U);
    }
}

TEST_CASE("Jack screamer decoder rejects a truncated bitmap") {
    const TemporaryBmpFile file {"jack_truncated"};
    {
        std::ofstream output(file.path, std::ios::binary);
        const std::vector<std::uint8_t> bytes {
            static_cast<std::uint8_t>('B'),
            static_cast<std::uint8_t>('M'),
            0U,
            0U,
        };
        output.write(
            reinterpret_cast<const char*>(bytes.data()),
            static_cast<std::streamsize>(bytes.size()));
    }
    const auto image =
        load_backrooms_jack_screamer_bmp(file.path);
    CHECK_FALSE(image.valid());
    CHECK_FALSE(image.error.empty());
}

TEST_CASE("Marlow screamer asset resolves and decodes with its own diagnostics") {
    const auto asset_path =
        resolve_backrooms_marlow_screamer_path(
            std::filesystem::current_path());
    REQUIRE_FALSE(asset_path.empty());
    const auto asset = load_backrooms_marlow_screamer_bmp(asset_path);
    INFO(asset.error);
    REQUIRE(asset.valid());
    CHECK(asset.width == 1280);
    CHECK(asset.height == 720);

    const TemporaryBmpFile missing_file {"marlow_missing_screamer"};
    const auto invalid =
        load_backrooms_marlow_screamer_bmp(missing_file.path);
    CHECK_FALSE(invalid.valid());
    CHECK(invalid.error.find("Marlow") != std::string::npos);
    CHECK(invalid.error.find("Jack") == std::string::npos);
}

TEST_CASE("Screamer decoder streams bottom-up and top-down BMP rows") {
    const TemporaryBmpFile bottom_up_file {"screamer_bottom_up"};
    auto bottom_up = make_test_bitmap(2, 2, 24U);
    // Je range d'abord la ligne du bas comme l'impose un BMP classique.
    const std::array<std::uint8_t, 16U> bottom_up_pixels {{
        255U, 0U, 0U, 255U, 255U, 255U, 0U, 0U,
        0U, 0U, 255U, 0U, 255U, 0U, 0U, 0U,
    }};
    std::copy(
        bottom_up_pixels.begin(),
        bottom_up_pixels.end(),
        bottom_up.begin() +
            static_cast<std::ptrdiff_t>(kTestBitmapHeaderBytes));
    write_bytes(bottom_up_file.path, bottom_up);

    const auto decoded_bottom_up =
        load_backrooms_jack_screamer_bmp(bottom_up_file.path);
    INFO(decoded_bottom_up.error);
    REQUIRE(decoded_bottom_up.valid());
    CHECK(decoded_bottom_up.width == 2);
    CHECK(decoded_bottom_up.height == 2);
    const std::vector<std::uint8_t> expected_bottom_up {
        255U, 0U, 0U, 255U,
        0U, 255U, 0U, 255U,
        0U, 0U, 255U, 255U,
        255U, 255U, 255U, 255U,
    };
    CHECK(decoded_bottom_up.rgba == expected_bottom_up);

    const TemporaryBmpFile top_down_file {"screamer_top_down"};
    auto top_down = make_test_bitmap(2, -1, 32U);
    const std::array<std::uint8_t, 8U> top_down_pixels {{
        0U, 255U, 255U, 0U,
        255U, 0U, 255U, 17U,
    }};
    std::copy(
        top_down_pixels.begin(),
        top_down_pixels.end(),
        top_down.begin() +
            static_cast<std::ptrdiff_t>(kTestBitmapHeaderBytes));
    write_bytes(top_down_file.path, top_down);

    const auto decoded_top_down =
        load_backrooms_marlow_screamer_bmp(top_down_file.path);
    INFO(decoded_top_down.error);
    REQUIRE(decoded_top_down.valid());
    const std::vector<std::uint8_t> expected_top_down {
        255U, 255U, 0U, 255U,
        255U, 0U, 255U, 255U,
    };
    CHECK(decoded_top_down.rgba == expected_top_down);
}

TEST_CASE("Screamer decoder rejects an oversized file before decoding") {
    const TemporaryBmpFile file {"screamer_oversized"};
    {
        std::ofstream output(
            file.path,
            std::ios::binary | std::ios::trunc);
        REQUIRE(output.is_open());
        constexpr auto oversized_file_bytes =
            std::uint64_t {66U} * 1'048'576U;
        output.seekp(
            static_cast<std::streamoff>(
                oversized_file_bytes - 1U));
        output.put('\0');
        REQUIRE(output.good());
    }

    const auto image =
        load_backrooms_jack_screamer_bmp(file.path);
    CHECK_FALSE(image.valid());
    CHECK(image.rgba.empty());
    CHECK(image.error.find("limite") != std::string::npos);
}

TEST_CASE("Screamer decoder rejects overflowing DIB metadata") {
    const TemporaryBmpFile file {"screamer_invalid_dib"};
    auto bytes = make_test_bitmap(1, 1, 24U);
    write_u32(
        bytes,
        14U,
        std::numeric_limits<std::uint32_t>::max());
    write_bytes(file.path, bytes);

    const auto image =
        load_backrooms_jack_screamer_bmp(file.path);
    CHECK_FALSE(image.valid());
    CHECK(image.rgba.empty());
    CHECK_FALSE(image.error.empty());
}

} // namespace valcraft
