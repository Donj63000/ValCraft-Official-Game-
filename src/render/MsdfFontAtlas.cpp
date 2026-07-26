#include "render/MsdfFontAtlas.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <fstream>
#include <limits>
#include <utility>

namespace valcraft {

namespace {

constexpr std::array<std::uint8_t, 8> kMagic {
    'V', 'C', 'M', 'S', 'D', 'F', 'A', '1',
};
constexpr std::size_t kGlyphRecordSize = 32U;
constexpr std::size_t kKerningRecordSize = 12U;
constexpr std::uint64_t kFnvOffset = 14695981039346656037ULL;
constexpr std::uint64_t kFnvPrime = 1099511628211ULL;

[[nodiscard]] auto read_u16(
    std::span<const std::uint8_t> bytes,
    std::size_t offset) noexcept -> std::uint16_t {
    return static_cast<std::uint16_t>(
        static_cast<std::uint16_t>(bytes[offset]) |
        static_cast<std::uint16_t>(
            static_cast<std::uint16_t>(bytes[offset + 1U]) << 8U));
}

[[nodiscard]] auto read_u32(
    std::span<const std::uint8_t> bytes,
    std::size_t offset) noexcept -> std::uint32_t {
    std::uint32_t value = 0;
    for (std::size_t index = 0; index < 4U; ++index) {
        value |= static_cast<std::uint32_t>(bytes[offset + index])
                 << static_cast<unsigned int>(index * 8U);
    }
    return value;
}

[[nodiscard]] auto read_i32(
    std::span<const std::uint8_t> bytes,
    std::size_t offset) noexcept -> std::int32_t {
    return static_cast<std::int32_t>(read_u32(bytes, offset));
}

[[nodiscard]] auto read_u64(
    std::span<const std::uint8_t> bytes,
    std::size_t offset) noexcept -> std::uint64_t {
    std::uint64_t value = 0;
    for (std::size_t index = 0; index < 8U; ++index) {
        value |= static_cast<std::uint64_t>(bytes[offset + index])
                 << static_cast<unsigned int>(index * 8U);
    }
    return value;
}

[[nodiscard]] auto checked_product(
    std::uint64_t lhs,
    std::uint64_t rhs,
    std::uint64_t& result) noexcept -> bool {
    if (lhs != 0U && rhs > (std::numeric_limits<std::uint64_t>::max)() / lhs) {
        return false;
    }
    result = lhs * rhs;
    return true;
}

[[nodiscard]] auto decode_utf8(std::string_view text) -> std::vector<char32_t> {
    std::vector<char32_t> result;
    result.reserve(text.size());
    for (std::size_t index = 0; index < text.size();) {
        const auto first = static_cast<std::uint8_t>(text[index]);
        char32_t codepoint = U'\uFFFD';
        std::size_t length = 1U;
        if (first < 0x80U) {
            codepoint = static_cast<char32_t>(first);
        } else if ((first & 0xE0U) == 0xC0U && index + 1U < text.size()) {
            codepoint = static_cast<char32_t>(first & 0x1FU);
            length = 2U;
        } else if ((first & 0xF0U) == 0xE0U && index + 2U < text.size()) {
            codepoint = static_cast<char32_t>(first & 0x0FU);
            length = 3U;
        } else if ((first & 0xF8U) == 0xF0U && index + 3U < text.size()) {
            codepoint = static_cast<char32_t>(first & 0x07U);
            length = 4U;
        }
        bool valid = length > 1U;
        for (std::size_t continuation = 1U;
             continuation < length && valid;
             ++continuation) {
            const auto byte =
                static_cast<std::uint8_t>(text[index + continuation]);
            valid = (byte & 0xC0U) == 0x80U;
            if (valid) {
                codepoint =
                    (codepoint << 6U) | static_cast<char32_t>(byte & 0x3FU);
            }
        }
        if (!valid && length > 1U) {
            codepoint = U'\uFFFD';
            length = 1U;
        }
        result.push_back(codepoint);
        index += length;
    }
    return result;
}

[[nodiscard]] auto failure(std::string message) -> MsdfFontAtlasLoadResult {
    MsdfFontAtlasLoadResult result {};
    result.error = std::move(message);
    return result;
}

} // namespace

auto msdf_font_payload_checksum(
    std::span<const std::uint8_t> bytes) noexcept -> std::uint64_t {
    auto checksum = kFnvOffset;
    for (const auto byte : bytes) {
        checksum ^= static_cast<std::uint64_t>(byte);
        checksum *= kFnvPrime;
    }
    return checksum;
}

auto load_msdf_font_atlas(
    std::span<const std::uint8_t> bytes) -> MsdfFontAtlasLoadResult {
    if (bytes.size() < kMsdfFontAtlasHeaderSize) {
        return failure("atlas header is truncated");
    }
    if (!std::equal(kMagic.begin(), kMagic.end(), bytes.begin())) {
        return failure("atlas magic is invalid");
    }
    const auto version = read_u32(bytes, 8U);
    const auto header_size = read_u32(bytes, 12U);
    const auto encoding = read_u32(bytes, 16U);
    const auto width = read_u32(bytes, 20U);
    const auto height = read_u32(bytes, 24U);
    const auto channels = read_u32(bytes, 28U);
    const auto mip_count = read_u32(bytes, 32U);
    const auto glyph_count = read_u32(bytes, 36U);
    const auto kerning_count = read_u32(bytes, 40U);
    const auto font_size_x64 = read_u32(bytes, 44U);
    const auto sdf_range_x64 = read_u32(bytes, 48U);
    const auto glyph_record_size = read_u32(bytes, 52U);
    const auto kerning_record_size = read_u32(bytes, 56U);
    const auto ascent_x64 = read_u32(bytes, 60U);
    const auto descent_x64 = read_u32(bytes, 64U);
    const auto line_gap_x64 = read_u32(bytes, 68U);
    const auto glyph_offset = read_u64(bytes, 72U);
    const auto kerning_offset = read_u64(bytes, 80U);
    const auto pixel_offset = read_u64(bytes, 88U);
    const auto expected_checksum = read_u64(bytes, 96U);

    if (version != kMsdfFontAtlasVersion ||
        header_size != kMsdfFontAtlasHeaderSize ||
        encoding != static_cast<std::uint32_t>(
            MsdfFontEncoding::MultiChannelSdfRgb8)) {
        return failure("atlas schema is unsupported");
    }
    if (width == 0U || height == 0U || width > 8192U || height > 8192U ||
        (width & (width - 1U)) != 0U ||
        (height & (height - 1U)) != 0U ||
        channels != 3U || mip_count == 0U || mip_count > 16U ||
        glyph_count < 95U || glyph_count > 4096U ||
        kerning_count > 1000000U || font_size_x64 == 0U ||
        sdf_range_x64 == 0U || glyph_record_size != kGlyphRecordSize ||
        kerning_record_size != kKerningRecordSize) {
        return failure("atlas dimensions or counts are invalid");
    }
    auto expected_mip_count = 1U;
    auto expected_mip_width = width;
    auto expected_mip_height = height;
    while (expected_mip_width > 1U || expected_mip_height > 1U) {
        expected_mip_width = std::max(1U, expected_mip_width / 2U);
        expected_mip_height = std::max(1U, expected_mip_height / 2U);
        ++expected_mip_count;
    }
    if (mip_count != expected_mip_count) {
        return failure("atlas mipmap count is not a complete chain");
    }

    std::uint64_t glyph_bytes = 0;
    std::uint64_t kerning_bytes = 0;
    if (!checked_product(glyph_count, glyph_record_size, glyph_bytes) ||
        !checked_product(kerning_count, kerning_record_size, kerning_bytes) ||
        glyph_offset != header_size ||
        kerning_offset != glyph_offset + glyph_bytes ||
        pixel_offset != kerning_offset + kerning_bytes ||
        pixel_offset > bytes.size()) {
        return failure("atlas table offsets are invalid");
    }
    const auto payload = bytes.subspan(static_cast<std::size_t>(glyph_offset));
    if (msdf_font_payload_checksum(payload) != expected_checksum) {
        return failure("atlas payload checksum does not match");
    }

    MsdfFontAtlasLoadResult result {};
    auto& atlas = result.atlas;
    atlas.metadata_ = {
        version,
        static_cast<MsdfFontEncoding>(encoding),
        width,
        height,
        channels,
        mip_count,
        static_cast<float>(font_size_x64) / 64.0F,
        static_cast<float>(sdf_range_x64) / 64.0F,
        static_cast<float>(ascent_x64) / 64.0F,
        -static_cast<float>(descent_x64) / 64.0F,
        static_cast<float>(line_gap_x64) / 64.0F,
        expected_checksum,
    };
    atlas.glyphs_.reserve(glyph_count);
    char32_t previous_codepoint = U'\0';
    for (std::uint32_t index = 0U; index < glyph_count; ++index) {
        const auto offset =
            static_cast<std::size_t>(glyph_offset) +
            static_cast<std::size_t>(index) * kGlyphRecordSize;
        const auto codepoint_value = read_u32(bytes, offset);
        const auto atlas_x = read_u16(bytes, offset + 4U);
        const auto atlas_y = read_u16(bytes, offset + 6U);
        const auto glyph_width = read_u16(bytes, offset + 8U);
        const auto glyph_height = read_u16(bytes, offset + 10U);
        const auto codepoint = static_cast<char32_t>(codepoint_value);
        if ((index > 0U && codepoint <= previous_codepoint) ||
            codepoint_value > 0x10FFFFU ||
            (codepoint_value >= 0xD800U && codepoint_value <= 0xDFFFU) ||
            glyph_width == 0U || glyph_height == 0U ||
            static_cast<std::uint32_t>(atlas_x) + glyph_width > width ||
            static_cast<std::uint32_t>(atlas_y) + glyph_height > height) {
            return failure("glyph table is unsorted or out of atlas bounds");
        }
        previous_codepoint = codepoint;
        atlas.glyphs_.push_back({
            codepoint,
            atlas_x,
            atlas_y,
            glyph_width,
            glyph_height,
            static_cast<float>(read_i32(bytes, offset + 12U)) / 64.0F,
            static_cast<float>(read_i32(bytes, offset + 16U)) / 64.0F,
            static_cast<float>(read_i32(bytes, offset + 20U)) / 64.0F,
            static_cast<float>(read_i32(bytes, offset + 24U)) / 64.0F,
            static_cast<float>(read_i32(bytes, offset + 28U)) / 64.0F,
        });
    }
    for (char32_t required = U' '; required <= U'~'; ++required) {
        if (atlas.find_glyph(required) == nullptr) {
            return failure("printable ASCII coverage is incomplete");
        }
    }

    atlas.kernings_.reserve(kerning_count);
    std::pair<char32_t, char32_t> previous_pair {};
    for (std::uint32_t index = 0U; index < kerning_count; ++index) {
        const auto offset =
            static_cast<std::size_t>(kerning_offset) +
            static_cast<std::size_t>(index) * kKerningRecordSize;
        const auto left = static_cast<char32_t>(read_u32(bytes, offset));
        const auto right = static_cast<char32_t>(read_u32(bytes, offset + 4U));
        const auto pair = std::pair {left, right};
        if ((index > 0U && pair <= previous_pair) ||
            atlas.find_glyph(left) == nullptr ||
            atlas.find_glyph(right) == nullptr) {
            return failure("kerning table is invalid");
        }
        previous_pair = pair;
        atlas.kernings_.push_back({
            left,
            right,
            static_cast<float>(read_i32(bytes, offset + 8U)) / 64.0F,
        });
    }

    std::uint64_t expected_pixel_bytes = 0;
    auto mip_width = width;
    auto mip_height = height;
    for (std::uint32_t level = 0U; level < mip_count; ++level) {
        std::uint64_t texel_count = 0;
        std::uint64_t level_bytes = 0;
        if (!checked_product(mip_width, mip_height, texel_count) ||
            !checked_product(texel_count, channels, level_bytes) ||
            level_bytes > (std::numeric_limits<std::size_t>::max)() ||
            expected_pixel_bytes >
                (std::numeric_limits<std::uint64_t>::max)() - level_bytes) {
            return failure("mipmap byte count overflows");
        }
        atlas.mip_levels_.push_back({
            mip_width,
            mip_height,
            static_cast<std::size_t>(expected_pixel_bytes),
            static_cast<std::size_t>(level_bytes),
        });
        expected_pixel_bytes += level_bytes;
        mip_width = std::max(1U, mip_width / 2U);
        mip_height = std::max(1U, mip_height / 2U);
    }
    if (mip_width != 1U || mip_height != 1U ||
        expected_pixel_bytes >
            static_cast<std::uint64_t>(bytes.size()) - pixel_offset ||
        pixel_offset + expected_pixel_bytes != bytes.size() ||
        pixel_offset >
            static_cast<std::uint64_t>(
                (std::numeric_limits<std::ptrdiff_t>::max)())) {
        return failure("mipmap chain is incomplete or truncated");
    }
    atlas.pixels_.assign(
        bytes.begin() + static_cast<std::ptrdiff_t>(pixel_offset),
        bytes.end());
    return result;
}

auto load_msdf_font_atlas_file(
    const std::filesystem::path& path) -> MsdfFontAtlasLoadResult {
    std::error_code error;
    const auto size = std::filesystem::file_size(path, error);
    if (error || size > (std::numeric_limits<std::size_t>::max)() ||
        size > static_cast<std::uintmax_t>(
            (std::numeric_limits<std::streamsize>::max)())) {
        return failure("font atlas file cannot be sized");
    }
    std::vector<std::uint8_t> bytes(static_cast<std::size_t>(size));
    std::ifstream input(path, std::ios::binary);
    if (!input) {
        return failure("font atlas file cannot be opened");
    }
    if (!bytes.empty()) {
        input.read(
            reinterpret_cast<char*>(bytes.data()),
            static_cast<std::streamsize>(bytes.size()));
        if (!input ||
            input.gcount() != static_cast<std::streamsize>(bytes.size())) {
            return failure("font atlas file is truncated");
        }
    }
    return load_msdf_font_atlas(bytes);
}

auto MsdfFontAtlas::metadata() const noexcept -> const MsdfFontMetadata& {
    return metadata_;
}

auto MsdfFontAtlas::glyphs() const noexcept -> const std::vector<MsdfGlyph>& {
    return glyphs_;
}

auto MsdfFontAtlas::kernings() const noexcept -> const std::vector<MsdfKerning>& {
    return kernings_;
}

auto MsdfFontAtlas::mip_levels() const noexcept
    -> const std::vector<MsdfMipLevel>& {
    return mip_levels_;
}

auto MsdfFontAtlas::pixels() const noexcept -> std::span<const std::uint8_t> {
    return pixels_;
}

auto MsdfFontAtlas::find_glyph(char32_t codepoint) const noexcept
    -> const MsdfGlyph* {
    const auto found = std::lower_bound(
        glyphs_.begin(),
        glyphs_.end(),
        codepoint,
        [](const MsdfGlyph& glyph, char32_t value) {
            return glyph.codepoint < value;
        });
    return found != glyphs_.end() && found->codepoint == codepoint
        ? &*found
        : nullptr;
}

auto MsdfFontAtlas::kerning(char32_t left, char32_t right) const noexcept
    -> float {
    const auto key = std::pair {left, right};
    const auto found = std::lower_bound(
        kernings_.begin(),
        kernings_.end(),
        key,
        [](const MsdfKerning& value, const auto& pair) {
            return std::pair {value.left, value.right} < pair;
        });
    return found != kernings_.end() &&
                   found->left == left && found->right == right
        ? found->adjustment
        : 0.0F;
}

auto MsdfFontAtlas::build_quads(
    std::string_view utf8,
    float origin_x,
    float baseline_y,
    float requested_pixel_height) const -> MsdfTextLayout {
    MsdfTextLayout layout {};
    if (metadata_.font_em_pixels <= 0.0F ||
        !std::isfinite(requested_pixel_height) ||
        requested_pixel_height <= 0.0F) {
        return layout;
    }
    const auto scale = requested_pixel_height / metadata_.font_em_pixels;
    const auto line_height =
        (metadata_.ascent - metadata_.descent + metadata_.line_gap) * scale;
    auto pen_x = origin_x;
    auto pen_y = baseline_y;
    auto maximum_width = 0.0F;
    std::size_t line_count = 1U;
    char32_t previous = U'\0';
    for (const auto decoded : decode_utf8(utf8)) {
        if (decoded == U'\n') {
            maximum_width = std::max(maximum_width, pen_x - origin_x);
            pen_x = origin_x;
            pen_y += line_height;
            previous = U'\0';
            ++line_count;
            continue;
        }
        const auto* glyph = find_glyph(decoded);
        if (glyph == nullptr) {
            glyph = find_glyph(U'?');
        }
        if (glyph == nullptr) {
            continue;
        }
        if (previous != U'\0') {
            pen_x += kerning(previous, glyph->codepoint) * scale;
        }
        if (glyph->codepoint != U' ') {
            layout.quads.push_back({
                glyph->codepoint,
                pen_x + glyph->plane_left * scale,
                pen_y - glyph->plane_top * scale,
                pen_x + glyph->plane_right * scale,
                pen_y - glyph->plane_bottom * scale,
                static_cast<float>(glyph->atlas_x) /
                    static_cast<float>(metadata_.width),
                static_cast<float>(glyph->atlas_y) /
                    static_cast<float>(metadata_.height),
                static_cast<float>(
                    static_cast<std::uint32_t>(glyph->atlas_x) +
                    glyph->atlas_width) /
                    static_cast<float>(metadata_.width),
                static_cast<float>(
                    static_cast<std::uint32_t>(glyph->atlas_y) +
                    glyph->atlas_height) /
                    static_cast<float>(metadata_.height),
            });
        }
        pen_x += glyph->advance * scale;
        previous = glyph->codepoint;
    }
    maximum_width = std::max(maximum_width, pen_x - origin_x);
    layout.width = maximum_width;
    layout.height = static_cast<float>(line_count) * line_height;
    layout.final_pen_x = pen_x;
    return layout;
}

} // namespace valcraft
