#include "render/MsdfFontAtlas.h"

#include <doctest/doctest.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <limits>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace valcraft {

namespace {

[[nodiscard]] auto find_font_atlas() -> std::filesystem::path {
    std::array<std::filesystem::path, 2> roots {
        std::filesystem::absolute(std::filesystem::path {__FILE__}).parent_path(),
        std::filesystem::current_path(),
    };
    for (auto root : roots) {
        for (int depth = 0; depth < 8; ++depth) {
            const auto candidate =
                root / "assets" / "fonts" / "valcraft_ui_font.msdfa";
            std::error_code error;
            if (std::filesystem::is_regular_file(candidate, error)) {
                return candidate;
            }
            if (!root.has_parent_path() || root.parent_path() == root) {
                break;
            }
            root = root.parent_path();
        }
    }
    return {};
}

[[nodiscard]] auto read_bytes(const std::filesystem::path& path)
    -> std::vector<std::uint8_t> {
    std::error_code error;
    const auto file_size = std::filesystem::file_size(path, error);
    if (error ||
        file_size > (std::numeric_limits<std::size_t>::max)()) {
        return {};
    }
    std::vector<std::uint8_t> bytes(static_cast<std::size_t>(file_size));
    std::ifstream input(path, std::ios::binary);
    if (!input) {
        return {};
    }
    input.read(
        reinterpret_cast<char*>(bytes.data()),
        static_cast<std::streamsize>(bytes.size()));
    return input ? bytes : std::vector<std::uint8_t> {};
}

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
    for (std::size_t index = 0; index < 4U; ++index) {
        bytes[offset + index] = static_cast<std::uint8_t>(
            (value >> static_cast<unsigned int>(index * 8U)) & 0xFFU);
    }
}

void write_u64(
    std::vector<std::uint8_t>& bytes,
    std::size_t offset,
    std::uint64_t value) {
    for (std::size_t index = 0; index < 8U; ++index) {
        bytes[offset + index] = static_cast<std::uint8_t>(
            (value >> static_cast<unsigned int>(index * 8U)) & 0xFFULL);
    }
}

void refresh_checksum(std::vector<std::uint8_t>& bytes) {
    REQUIRE(bytes.size() >= kMsdfFontAtlasHeaderSize);
    write_u64(
        bytes,
        96U,
        msdf_font_payload_checksum(
            std::span<const std::uint8_t>(bytes).subspan(
                kMsdfFontAtlasHeaderSize)));
}

[[nodiscard]] auto glyph_ink_bounds(
    const MsdfFontAtlas& atlas,
    char32_t codepoint)
    -> std::optional<std::array<std::uint32_t, 4>> {
    const auto* glyph = atlas.find_glyph(codepoint);
    if (glyph == nullptr || atlas.mip_levels().empty()) {
        return std::nullopt;
    }
    const auto& base_mip = atlas.mip_levels().front();
    auto minimum_x =
        static_cast<std::uint32_t>(glyph->atlas_width);
    auto minimum_y =
        static_cast<std::uint32_t>(glyph->atlas_height);
    auto maximum_x = std::uint32_t {0U};
    auto maximum_y = std::uint32_t {0U};
    auto found_ink = false;
    for (std::uint32_t local_y = 0U;
         local_y < glyph->atlas_height;
         ++local_y) {
        for (std::uint32_t local_x = 0U;
             local_x < glyph->atlas_width;
             ++local_x) {
            const auto atlas_x =
                static_cast<std::uint32_t>(glyph->atlas_x) + local_x;
            const auto atlas_y =
                static_cast<std::uint32_t>(glyph->atlas_y) + local_y;
            const auto pixel_offset =
                base_mip.byte_offset +
                (static_cast<std::size_t>(atlas_y) *
                     base_mip.width +
                 atlas_x) *
                    3U;
            REQUIRE(pixel_offset + 2U < atlas.pixels().size());
            auto channels = std::array {
                atlas.pixels()[pixel_offset],
                atlas.pixels()[pixel_offset + 1U],
                atlas.pixels()[pixel_offset + 2U],
            };
            std::sort(channels.begin(), channels.end());
            if (channels[1] < 128U) {
                continue;
            }
            found_ink = true;
            minimum_x = std::min(minimum_x, local_x);
            minimum_y = std::min(minimum_y, local_y);
            maximum_x = std::max(maximum_x, local_x);
            maximum_y = std::max(maximum_y, local_y);
        }
    }
    if (!found_ink) {
        return std::nullopt;
    }
    return std::array<std::uint32_t, 4> {
        minimum_x,
        minimum_y,
        maximum_x,
        maximum_y,
    };
}

} // namespace

TEST_CASE("MSDF atlas loads its versioned multi-channel payload and full mip chain") {
    const auto path = find_font_atlas();
    REQUIRE(!path.empty());
    const auto loaded = load_msdf_font_atlas_file(path);
    INFO(loaded.error);
    REQUIRE(static_cast<bool>(loaded));

    const auto& atlas = loaded.atlas;
    CHECK(atlas.metadata().version == kMsdfFontAtlasVersion);
    CHECK(
        atlas.metadata().encoding ==
        MsdfFontEncoding::MultiChannelSdfRgb8);
    CHECK(atlas.metadata().width == 1024U);
    CHECK(atlas.metadata().height == 1024U);
    CHECK(atlas.metadata().channels == 3U);
    CHECK(atlas.metadata().mip_count == 11U);
    CHECK(atlas.glyphs().size() == 141U);
    CHECK(atlas.metadata().font_em_pixels == doctest::Approx(44.0F));
    CHECK(atlas.metadata().sdf_range_pixels == doctest::Approx(8.0F));
    REQUIRE(atlas.mip_levels().size() == 11U);
    CHECK(atlas.mip_levels().front().width == 1024U);
    CHECK(atlas.mip_levels().front().height == 1024U);
    CHECK(atlas.mip_levels().back().width == 1U);
    CHECK(atlas.mip_levels().back().height == 1U);

    std::size_t expected_offset = 0U;
    for (const auto& mip : atlas.mip_levels()) {
        CHECK(mip.byte_offset == expected_offset);
        CHECK(
            mip.byte_size ==
            static_cast<std::size_t>(mip.width) *
                static_cast<std::size_t>(mip.height) * 3U);
        expected_offset += mip.byte_size;
    }
    CHECK(expected_offset == atlas.pixels().size());
    CHECK(
        std::any_of(
            atlas.pixels().begin(),
            atlas.pixels().end() - 2,
            [&atlas](const std::uint8_t& value) {
                const auto index = static_cast<std::size_t>(
                    &value - atlas.pixels().data());
                return atlas.pixels()[index] != atlas.pixels()[index + 1U] ||
                       atlas.pixels()[index + 1U] !=
                           atlas.pixels()[index + 2U];
            }));
}

TEST_CASE("MSDF atlas covers printable ASCII and useful French glyphs") {
    const auto loaded = load_msdf_font_atlas_file(find_font_atlas());
    REQUIRE(static_cast<bool>(loaded));
    for (char32_t codepoint = U' '; codepoint <= U'~'; ++codepoint) {
        CAPTURE(static_cast<std::uint32_t>(codepoint));
        CHECK(loaded.atlas.find_glyph(codepoint) != nullptr);
    }
    for (const auto codepoint : std::array {
             U'\u00E0', U'\u00E2', U'\u00E7', U'\u00E9', U'\u00E8',
             U'\u00EA', U'\u00EB', U'\u00EE', U'\u00EF', U'\u00F4',
             U'\u00F9', U'\u00FB', U'\u00FC', U'\u00FF', U'\u0153',
             U'\u0152', U'\u20AC', U'\u00AB', U'\u00BB', U'\u2019',
             U'\u2014', U'\u2026',
         }) {
        CAPTURE(static_cast<std::uint32_t>(codepoint));
        CHECK(loaded.atlas.find_glyph(codepoint) != nullptr);
    }
    CHECK(loaded.atlas.kerning(U'A', U'V') < 0.0F);
    CHECK(loaded.atlas.kerning(U'T', U'o') < 0.0F);
}

TEST_CASE("MSDF glyph ink keeps a safe border and is never baseline-clipped") {
    const auto loaded = load_msdf_font_atlas_file(find_font_atlas());
    REQUIRE(static_cast<bool>(loaded));

    // Je couvre ascendantes, descendantes, chiffres, minuscules et accents :
    // l'ancienne ancre d'ascender coupait précisément leur bas de cellule.
    for (const auto codepoint :
         std::array {U'A', U'V', U'0', U'g', U'y', U'\u00E9'}) {
        CAPTURE(static_cast<std::uint32_t>(codepoint));
        const auto bounds =
            glyph_ink_bounds(loaded.atlas, codepoint);
        REQUIRE(bounds.has_value());
        const auto* glyph =
            loaded.atlas.find_glyph(codepoint);
        REQUIRE(glyph != nullptr);
        CHECK((*bounds)[0] >= 2U);
        CHECK((*bounds)[1] >= 2U);
        CHECK((*bounds)[2] + 2U <
              static_cast<std::uint32_t>(glyph->atlas_width));
        CHECK((*bounds)[3] + 2U <
              static_cast<std::uint32_t>(glyph->atlas_height));
    }
}

TEST_CASE("MSDF layout exposes deterministic UTF-8 quads without UI interaction state") {
    const auto loaded = load_msdf_font_atlas_file(find_font_atlas());
    REQUIRE(static_cast<bool>(loaded));
    const std::string text =
        "\xC3\x89t\xC3\xA9 AV\n\xC3\x87" "a";
    const auto first = loaded.atlas.build_quads(
        text,
        100.0F,
        50.0F,
        22.0F);
    const auto second = loaded.atlas.build_quads(
        text,
        100.0F,
        50.0F,
        22.0F);

    CHECK(first.quads == second.quads);
    CHECK(first.width == second.width);
    CHECK(first.height == second.height);
    REQUIRE(first.quads.size() == 7U);
    CHECK(first.width > 0.0F);
    CHECK(first.height > 22.0F);
    for (const auto& quad : first.quads) {
        CHECK(quad.x0 < quad.x1);
        CHECK(quad.y0 < quad.y1);
        CHECK(quad.u0 >= 0.0F);
        CHECK(quad.v0 >= 0.0F);
        CHECK(quad.u1 <= 1.0F);
        CHECK(quad.v1 <= 1.0F);
        CHECK(quad.u0 < quad.u1);
        CHECK(quad.v0 < quad.v1);
    }
    const auto invalid_size = loaded.atlas.build_quads(
        "texte",
        0.0F,
        0.0F,
        -1.0F);
    CHECK(invalid_size.quads.empty());
}

TEST_CASE("MSDF atlas loading is byte deterministic") {
    const auto bytes = read_bytes(find_font_atlas());
    REQUIRE(!bytes.empty());
    const auto first = load_msdf_font_atlas(bytes);
    const auto second = load_msdf_font_atlas(bytes);
    REQUIRE(static_cast<bool>(first));
    REQUIRE(static_cast<bool>(second));
    CHECK(first.atlas.glyphs() == second.atlas.glyphs());
    CHECK(first.atlas.kernings() == second.atlas.kernings());
    CHECK(first.atlas.mip_levels() == second.atlas.mip_levels());
    CHECK(std::ranges::equal(first.atlas.pixels(), second.atlas.pixels()));
    CHECK(
        first.atlas.metadata().payload_checksum ==
        second.atlas.metadata().payload_checksum);
}

TEST_CASE("MSDF atlas rejects corrupt schema checksum channels and glyph bounds") {
    const auto original = read_bytes(find_font_atlas());
    REQUIRE(original.size() > kMsdfFontAtlasHeaderSize + 32U);

    auto bad_magic = original;
    bad_magic[0] ^= 0xFFU;
    CHECK(!static_cast<bool>(load_msdf_font_atlas(bad_magic)));

    auto bad_version = original;
    write_u32(bad_version, 8U, kMsdfFontAtlasVersion + 1U);
    CHECK(!static_cast<bool>(load_msdf_font_atlas(bad_version)));

    auto bad_channels = original;
    write_u32(bad_channels, 28U, 4U);
    CHECK(!static_cast<bool>(load_msdf_font_atlas(bad_channels)));

    auto bad_checksum = original;
    bad_checksum.back() ^= 1U;
    CHECK(!static_cast<bool>(load_msdf_font_atlas(bad_checksum)));

    auto bad_glyph_bounds = original;
    write_u16(
        bad_glyph_bounds,
        kMsdfFontAtlasHeaderSize + 4U,
        (std::numeric_limits<std::uint16_t>::max)());
    refresh_checksum(bad_glyph_bounds);
    CHECK(!static_cast<bool>(load_msdf_font_atlas(bad_glyph_bounds)));

    CHECK(!static_cast<bool>(load_msdf_font_atlas(
        std::span<const std::uint8_t>(original).first(20U))));
}

} // namespace valcraft
