#include "render/BackroomsJackScreamer.h"

#include <doctest/doctest.h>

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <vector>

namespace valcraft {

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
    const auto path =
        std::filesystem::temp_directory_path() /
        "valcraft_jack_truncated.bmp";
    {
        std::ofstream output(path, std::ios::binary);
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
        load_backrooms_jack_screamer_bmp(path);
    CHECK_FALSE(image.valid());
    CHECK_FALSE(image.error.empty());
    std::error_code ignored;
    std::filesystem::remove(path, ignored);
}

} // namespace valcraft
