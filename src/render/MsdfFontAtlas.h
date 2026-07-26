#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace valcraft {

inline constexpr std::uint32_t kMsdfFontAtlasVersion = 1U;
inline constexpr std::size_t kMsdfFontAtlasHeaderSize = 104U;

enum class MsdfFontEncoding : std::uint32_t {
    MultiChannelSdfRgb8 = 1U,
};

struct MsdfFontMetadata {
    std::uint32_t version = 0;
    MsdfFontEncoding encoding = MsdfFontEncoding::MultiChannelSdfRgb8;
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    std::uint32_t channels = 0;
    std::uint32_t mip_count = 0;
    float font_em_pixels = 0.0F;
    float sdf_range_pixels = 0.0F;
    float ascent = 0.0F;
    float descent = 0.0F;
    float line_gap = 0.0F;
    std::uint64_t payload_checksum = 0;
};

struct MsdfGlyph {
    char32_t codepoint = U'\0';
    std::uint16_t atlas_x = 0;
    std::uint16_t atlas_y = 0;
    std::uint16_t atlas_width = 0;
    std::uint16_t atlas_height = 0;
    float advance = 0.0F;
    float plane_left = 0.0F;
    float plane_top = 0.0F;
    float plane_right = 0.0F;
    float plane_bottom = 0.0F;

    auto operator==(const MsdfGlyph&) const -> bool = default;
};

struct MsdfKerning {
    char32_t left = U'\0';
    char32_t right = U'\0';
    float adjustment = 0.0F;

    auto operator==(const MsdfKerning&) const -> bool = default;
};

struct MsdfMipLevel {
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    std::size_t byte_offset = 0;
    std::size_t byte_size = 0;

    auto operator==(const MsdfMipLevel&) const -> bool = default;
};

struct MsdfTextQuad {
    char32_t codepoint = U'\0';
    float x0 = 0.0F;
    float y0 = 0.0F;
    float x1 = 0.0F;
    float y1 = 0.0F;
    float u0 = 0.0F;
    float v0 = 0.0F;
    float u1 = 0.0F;
    float v1 = 0.0F;

    auto operator==(const MsdfTextQuad&) const -> bool = default;
};

struct MsdfTextLayout {
    std::vector<MsdfTextQuad> quads;
    float width = 0.0F;
    float height = 0.0F;
    float final_pen_x = 0.0F;
};

struct MsdfFontAtlasLoadResult;

class MsdfFontAtlas {
public:
    [[nodiscard]] auto metadata() const noexcept -> const MsdfFontMetadata&;
    [[nodiscard]] auto glyphs() const noexcept -> const std::vector<MsdfGlyph>&;
    [[nodiscard]] auto kernings() const noexcept -> const std::vector<MsdfKerning>&;
    [[nodiscard]] auto mip_levels() const noexcept -> const std::vector<MsdfMipLevel>&;
    [[nodiscard]] auto pixels() const noexcept -> std::span<const std::uint8_t>;
    [[nodiscard]] auto find_glyph(char32_t codepoint) const noexcept -> const MsdfGlyph*;
    [[nodiscard]] auto kerning(char32_t left, char32_t right) const noexcept -> float;

    // Je ne modifie aucun etat UI : je transforme seulement le texte en quads
    // aux memes coordonnees d'origine et laisse les zones cliquables au HUD.
    [[nodiscard]] auto build_quads(
        std::string_view utf8,
        float origin_x,
        float baseline_y,
        float requested_pixel_height) const -> MsdfTextLayout;

private:
    friend struct MsdfFontAtlasLoadResult;
    friend auto load_msdf_font_atlas(std::span<const std::uint8_t>)
        -> MsdfFontAtlasLoadResult;

    MsdfFontMetadata metadata_ {};
    std::vector<MsdfGlyph> glyphs_;
    std::vector<MsdfKerning> kernings_;
    std::vector<MsdfMipLevel> mip_levels_;
    std::vector<std::uint8_t> pixels_;
};

struct MsdfFontAtlasLoadResult {
    MsdfFontAtlas atlas {};
    std::string error;

    [[nodiscard]] explicit operator bool() const noexcept {
        return error.empty();
    }
};

[[nodiscard]] auto msdf_font_payload_checksum(
    std::span<const std::uint8_t> bytes) noexcept -> std::uint64_t;
[[nodiscard]] auto load_msdf_font_atlas(
    std::span<const std::uint8_t> bytes) -> MsdfFontAtlasLoadResult;
[[nodiscard]] auto load_msdf_font_atlas_file(
    const std::filesystem::path& path) -> MsdfFontAtlasLoadResult;

} // namespace valcraft
