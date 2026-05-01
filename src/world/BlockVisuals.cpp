#include "world/BlockVisuals.h"
#include "world/GeneratedBlockTextureTiles.h"

#include <algorithm>
#include <array>
#include <cmath>

namespace valcraft {

namespace {

constexpr float kFullTurnRadians = 6.28318530718F;
constexpr float kTileEdgeSpan = static_cast<float>(kBlockAtlasTileSize - 1);

auto saturate(float value) noexcept -> float {
    return std::clamp(value, 0.0F, 1.0F);
}

auto to_byte(float value) noexcept -> std::uint8_t {
    return static_cast<std::uint8_t>(std::clamp(value, 0.0F, 255.0F));
}

auto make_rgba(float r, float g, float b, float a = 255.0F) noexcept -> std::array<std::uint8_t, 4> {
    return {to_byte(r), to_byte(g), to_byte(b), to_byte(a)};
}

auto hash_to_unit(int x, int y, int seed) noexcept -> float {
    auto value = static_cast<std::uint32_t>(x) * 374761393U;
    value ^= static_cast<std::uint32_t>(y) * 668265263U;
    value ^= static_cast<std::uint32_t>(seed) * 2246822519U;
    value = (value ^ (value >> 13U)) * 1274126177U;
    value ^= value >> 16U;
    return static_cast<float>(value & 0xFFFFU) / 65535.0F;
}

auto tile_noise(int x, int y, int seed) noexcept -> float {
    const auto coarse = hash_to_unit(x / 2 + seed, y / 2 + seed * 3, seed + 17);
    const auto fine = hash_to_unit(x + seed * 11, y + seed * 5, seed + 29);
    return coarse * 0.46F + fine * 0.54F;
}

auto radial_falloff(float x, float y, float center_x, float center_y, float radius) noexcept -> float {
    const auto dx = x - center_x;
    const auto dy = y - center_y;
    const auto distance = std::sqrt(dx * dx + dy * dy);
    return saturate(1.0F - distance / std::max(radius, 0.001F));
}

auto line_mask(float value, float center, float half_width) noexcept -> float {
    return saturate(1.0F - std::abs(value - center) / std::max(half_width, 0.001F));
}

auto distance_to_segment(float px, float py, float ax, float ay, float bx, float by) noexcept -> float {
    const auto abx = bx - ax;
    const auto aby = by - ay;
    const auto apx = px - ax;
    const auto apy = py - ay;
    const auto ab_length_squared = abx * abx + aby * aby;
    if (ab_length_squared <= 1.0e-5F) {
        const auto dx = px - ax;
        const auto dy = py - ay;
        return std::sqrt(dx * dx + dy * dy);
    }

    const auto projection = std::clamp((apx * abx + apy * aby) / ab_length_squared, 0.0F, 1.0F);
    const auto closest_x = ax + abx * projection;
    const auto closest_y = ay + aby * projection;
    const auto dx = px - closest_x;
    const auto dy = py - closest_y;
    return std::sqrt(dx * dx + dy * dy);
}

struct CrackStroke {
    float ax = 0.0F;
    float ay = 0.0F;
    float bx = 0.0F;
    float by = 0.0F;
    int first_stage = 0;
    float thickness = 0.7F;
};

constexpr std::array<CrackStroke, 10> kCrackStrokes {{
    {7.8F, 1.0F, 7.2F, 14.2F, 0, 0.95F},
    {7.6F, 6.6F, 12.8F, 2.0F, 1, 0.78F},
    {7.4F, 7.4F, 2.0F, 3.2F, 2, 0.74F},
    {7.2F, 8.3F, 12.9F, 13.6F, 3, 0.76F},
    {6.8F, 9.2F, 2.6F, 14.0F, 4, 0.78F},
    {3.0F, 7.8F, 12.8F, 7.2F, 5, 0.68F},
    {4.0F, 2.4F, 2.2F, 6.2F, 5, 0.58F},
    {10.6F, 2.8F, 13.6F, 6.2F, 6, 0.56F},
    {4.2F, 11.0F, 1.6F, 13.0F, 6, 0.54F},
    {10.4F, 10.6F, 14.1F, 11.8F, 7, 0.52F},
}};

auto crack_strength(int x, int y, int stage) noexcept -> float {
    const auto px = static_cast<float>(x) + 0.5F;
    const auto py = static_cast<float>(y) + 0.5F;
    auto strongest = 0.0F;

    for (const auto& stroke : kCrackStrokes) {
        if (stage < stroke.first_stage) {
            continue;
        }

        const auto distance = distance_to_segment(px, py, stroke.ax, stroke.ay, stroke.bx, stroke.by);
        const auto stage_growth = static_cast<float>(stage - stroke.first_stage) * 0.08F;
        const auto jitter = (tile_noise(x, y, 70 + stroke.first_stage * 3 + stage) - 0.5F) * 0.16F;
        const auto thickness = std::max(0.20F, stroke.thickness + stage_growth + jitter);
        const auto strength = saturate(1.0F - distance / thickness);
        strongest = std::max(strongest, strength);
    }

    if (stage >= 4) {
        const auto chip_noise = tile_noise(x, y, 101 + stage * 5);
        const auto center_falloff = radial_falloff(px, py, 7.5F, 7.5F, 8.6F);
        const auto chipped =
            chip_noise > (0.91F - static_cast<float>(stage - 4) * 0.035F) ? center_falloff * 0.72F : 0.0F;
        strongest = std::max(strongest, chipped);
    }

    return strongest;
}

void set_texel(std::vector<std::uint8_t>& pixels, int x, int y, const std::array<std::uint8_t, 4>& rgba) {
    const auto index = static_cast<std::size_t>((y * kBlockAtlasSize + x) * 4);
    pixels[index + 0] = rgba[0];
    pixels[index + 1] = rgba[1];
    pixels[index + 2] = rgba[2];
    pixels[index + 3] = rgba[3];
}

void set_texel(std::vector<std::uint8_t>& pixels, int atlas_size, int x, int y, const std::array<std::uint8_t, 4>& rgba) {
    const auto index = static_cast<std::size_t>((y * atlas_size + x) * 4);
    pixels[index + 0] = rgba[0];
    pixels[index + 1] = rgba[1];
    pixels[index + 2] = rgba[2];
    pixels[index + 3] = rgba[3];
}

template <typename ColorFn>
void fill_tile(std::vector<std::uint8_t>& pixels, int tile_x, int tile_y, const ColorFn& color_fn) {
    const auto start_x = tile_x * kBlockAtlasTileSize;
    const auto start_y = tile_y * kBlockAtlasTileSize;
    for (int y = 0; y < kBlockAtlasTileSize; ++y) {
        for (int x = 0; x < kBlockAtlasTileSize; ++x) {
            set_texel(pixels, start_x + x, start_y + y, color_fn(x, y));
        }
    }
}

template <typename ColorFn>
void fill_tile(std::vector<std::uint8_t>& pixels,
               int atlas_size,
               int tile_size,
               int tile_x,
               int tile_y,
               const ColorFn& color_fn) {
    const auto start_x = tile_x * tile_size;
    const auto start_y = tile_y * tile_size;
    for (int y = 0; y < tile_size; ++y) {
        for (int x = 0; x < tile_size; ++x) {
            set_texel(pixels, atlas_size, start_x + x, start_y + y, color_fn(x, y));
        }
    }
}

auto unpack_packed_rgba(std::uint32_t packed_rgba) noexcept -> std::array<std::uint8_t, 4> {
    return {
        static_cast<std::uint8_t>((packed_rgba >> 24U) & 0xFFU),
        static_cast<std::uint8_t>((packed_rgba >> 16U) & 0xFFU),
        static_cast<std::uint8_t>((packed_rgba >> 8U) & 0xFFU),
        static_cast<std::uint8_t>(packed_rgba & 0xFFU),
    };
}

void blit_packed_tile(std::vector<std::uint8_t>& pixels,
                      int tile_x,
                      int tile_y,
                      const generated_block_texture_tiles::PackedTile& tile) {
    const auto start_x = tile_x * kBlockAtlasTileSize;
    const auto start_y = tile_y * kBlockAtlasTileSize;
    for (int y = 0; y < kBlockAtlasTileSize; ++y) {
        for (int x = 0; x < kBlockAtlasTileSize; ++x) {
            const auto packed = tile[static_cast<std::size_t>(y * kBlockAtlasTileSize + x)];
            set_texel(pixels, start_x + x, start_y + y, unpack_packed_rgba(packed));
        }
    }
}

void overwrite_antique_theme_tiles(std::vector<std::uint8_t>& pixels) {
    fill_tile(pixels, 3, 0, [](int x, int y) {
        const auto noise = tile_noise(x, y, 103);
        const auto soft = tile_noise(x + 4, y + 9, 137);
        const auto vein = line_mask(std::sin((static_cast<float>(x) * 0.62F + static_cast<float>(y) * 0.34F) + soft * 2.4F), 0.0F, 0.12F);
        const auto chisel = ((x == 0 || y == 0 || x == 15 || y == 15) ? -14.0F : 0.0F) + ((x == 7 || y == 8) ? -5.0F : 0.0F);
        return make_rgba(
            194.0F + noise * 28.0F - vein * 22.0F + chisel,
            184.0F + soft * 26.0F - vein * 18.0F + chisel * 0.75F,
            162.0F + noise * 18.0F - vein * 12.0F + chisel * 0.45F,
            255.0F);
    });

    fill_tile(pixels, 4, 0, [](int x, int y) {
        const auto noise = tile_noise(x, y, 109);
        const auto plaster_wave = std::sin(static_cast<float>(x + y) * 0.52F + noise * 3.0F);
        const auto hairline = line_mask(plaster_wave, 0.0F, 0.055F) * (tile_noise(x + 3, y + 5, 151) > 0.72F ? 1.0F : 0.0F);
        return make_rgba(
            214.0F + noise * 24.0F - hairline * 34.0F,
            198.0F + noise * 19.0F - hairline * 28.0F,
            162.0F + noise * 14.0F - hairline * 20.0F,
            255.0F);
    });

    fill_tile(pixels, 5, 0, [](int x, int y) {
        const auto block_x = x / 4;
        const auto block_y = y / 4;
        const auto mortar = x % 4 == 0 || y % 4 == 0;
        const auto offset = static_cast<float>((block_x * 17 + block_y * 29) % 19) - 9.0F;
        const auto chip = tile_noise(x, y, 127) > 0.78F ? tile_noise(x + 11, y + 13, 173) * 18.0F : 0.0F;
        return make_rgba(
            176.0F + offset + chip - (mortar ? 34.0F : 0.0F),
            166.0F + offset * 0.8F + chip * 0.72F - (mortar ? 29.0F : 0.0F),
            143.0F + offset * 0.55F + chip * 0.42F - (mortar ? 20.0F : 0.0F),
            255.0F);
    });

    fill_tile(pixels, 6, 0, [](int x, int y) {
        const auto pebble = tile_noise(x * 2, y * 2, 131);
        const auto warm = tile_noise(x + 5, y + 7, 139);
        const auto edge = ((x + y) % 5 == 0) ? -14.0F : 0.0F;
        return make_rgba(
            154.0F + pebble * 50.0F + warm * 10.0F + edge,
            142.0F + pebble * 42.0F + edge * 0.78F,
            124.0F + pebble * 32.0F + edge * 0.56F,
            255.0F);
    });

    fill_tile(pixels, 7, 0, [](int x, int y) {
        const auto noise = tile_noise(x, y, 149);
        const auto patina = tile_noise(x / 2 + 3, y / 2 + 5, 181);
        const auto growth = patina > 0.54F ? (patina - 0.54F) * 2.1F : 0.0F;
        const auto crack = line_mask(std::sin(static_cast<float>(x) * 0.70F - static_cast<float>(y) * 0.42F + noise * 2.2F), 0.0F, 0.075F);
        return make_rgba(
            168.0F + noise * 22.0F - crack * 24.0F - growth * 34.0F,
            164.0F + noise * 18.0F - crack * 16.0F + growth * 28.0F,
            134.0F + noise * 14.0F - crack * 12.0F - growth * 22.0F,
            255.0F);
    });

    fill_tile(pixels, 2, 1, [](int x, int y) {
        const auto tile_row = y / 4;
        const auto seam = y % 4 == 0 || x == 0 || x == 15;
        const auto curve = std::sin((static_cast<float>(x) / 15.0F) * kFullTurnRadians) * 8.0F;
        const auto weathering = tile_noise(x, y + tile_row * 5, 197) * 17.0F;
        return make_rgba(
            154.0F + weathering + curve - (seam ? 28.0F : 0.0F),
            78.0F + weathering * 0.48F - (seam ? 16.0F : 0.0F),
            42.0F + weathering * 0.26F - (seam ? 8.0F : 0.0F),
            255.0F);
    });

    fill_tile(pixels, 1, 4, [](int x, int y) {
        const auto border = x == 0 || x == 15 || y == 0 || y == 15;
        const auto lattice = x == 7 || x == 8 || y == 7 || y == 8 ||
                             (((x + y) % 9) == 0 && (x <= 3 || x >= 12 || y <= 3 || y >= 12));
        const auto reflection = x <= 3 && y <= 3;
        if (border || lattice) {
            const auto bronze_highlight = ((x + y) % 5 == 0) ? 12.0F : 0.0F;
            return make_rgba(118.0F + bronze_highlight, 78.0F + bronze_highlight * 0.65F, 42.0F + bronze_highlight * 0.35F, 255.0F);
        }
        if (reflection) {
            return make_rgba(118.0F, 158.0F, 194.0F, 196.0F);
        }
        return make_rgba(0.0F, 0.0F, 0.0F, 0.0F);
    });
}

} // namespace

auto build_block_atlas_pixels() -> std::vector<std::uint8_t> {
    using namespace generated_block_texture_tiles;

    std::vector<std::uint8_t> pixels(static_cast<std::size_t>(kBlockAtlasSize * kBlockAtlasSize * 4), 0);

    // The terrain family below is baked from the authored PNG sources in Textures/.
    // The renderer and mesher still consume the exact same 8x8 block atlas as before.
    blit_packed_tile(pixels, 0, 0, kGrassTopTile);
    blit_packed_tile(pixels, 1, 0, kGrassSideTile);
    blit_packed_tile(pixels, 2, 0, kDirtTile);
    blit_packed_tile(pixels, 3, 0, kStoneTile);
    blit_packed_tile(pixels, 4, 0, kSandTile);
    blit_packed_tile(pixels, 5, 0, kCobblestoneTile);
    blit_packed_tile(pixels, 6, 0, kGravelTile);
    blit_packed_tile(pixels, 7, 0, kMossyStoneTile);

    blit_packed_tile(pixels, 0, 1, kWoodSideTile);
    blit_packed_tile(pixels, 1, 1, kWoodTopTile);
    blit_packed_tile(pixels, 2, 1, kPlanksTile);
    blit_packed_tile(pixels, 3, 1, kLeavesTile);
    blit_packed_tile(pixels, 4, 1, kPineWoodSideTile);
    blit_packed_tile(pixels, 5, 1, kPineWoodTopTile);
    blit_packed_tile(pixels, 6, 1, kPineLeavesTile);

    blit_packed_tile(pixels, 0, 2, kSnowTopTile);
    blit_packed_tile(pixels, 1, 2, kSnowSideTile);

    overwrite_antique_theme_tiles(pixels);

    fill_tile(pixels, 5, 2, [](int x, int y) {
        const auto rib = (x % 4 == 0) ? -18.0F : 0.0F;
        const auto thorn = (y % 5 == 2 && (x % 4 == 1 || x % 4 == 3)) ? 10.0F : 0.0F;
        const auto base = tile_noise(x, y, 20) * 18.0F;
        return make_rgba(44.0F + base + thorn * 0.4F, 120.0F + base - rib + thorn, 48.0F + base * 0.45F, 255.0F);
    });
    fill_tile(pixels, 6, 2, [](int x, int y) {
        const auto dx = static_cast<float>(x) - 7.5F;
        const auto dy = static_cast<float>(y) - 7.5F;
        const auto ring = std::sin(std::sqrt(dx * dx + dy * dy) * 1.9F) * 10.0F;
        return make_rgba(78.0F + ring * 0.25F, 152.0F + ring * 0.6F, 74.0F + ring * 0.25F, 255.0F);
    });
    fill_tile(pixels, 7, 2, [](int x, int y) {
        const auto wave = std::sin((static_cast<float>(x) + static_cast<float>(y) * 0.85F) * 0.7F) * 10.0F;
        const auto shimmer = std::cos((static_cast<float>(x) - static_cast<float>(y) * 0.55F) * 0.9F) * 7.0F;
        const auto noise = tile_noise(x, y, 23) * 16.0F;
        const auto foam = (y < 2 || x < 2 || x > 13 || y > 13) ? 10.0F : 0.0F;
        return make_rgba(
            32.0F + noise * 0.5F + shimmer * 0.2F,
            92.0F + noise + wave * 0.25F + foam * 0.6F,
            158.0F + noise * 1.2F + wave * 0.4F + foam,
            186.0F);
    });

    fill_tile(pixels, 0, 3, [](int x, int y) {
        const auto head = y >= 1 && y <= 5 && x >= 5 && x <= 10;
        const auto shaft = y >= 6 && x >= 6 && x <= 9;
        const auto white_core = y >= 2 && y <= 3 && x >= 6 && x <= 9;
        const auto hot_band = y >= 3 && y <= 4 && x >= 5 && x <= 10;
        const auto shaft_ring = shaft && (y == 9 || y == 12);

        if (head) {
            const auto edge_shadow = (x == 5 || x == 10 || y == 1 || y == 5) ? -18.0F : 0.0F;
            if (white_core) {
                return make_rgba(252.0F + edge_shadow * 0.35F, 244.0F + edge_shadow * 0.25F, 196.0F, 255.0F);
            }
            const auto glow = hot_band ? 18.0F : 0.0F;
            return make_rgba(230.0F + glow + edge_shadow, 188.0F + glow * 0.6F + edge_shadow * 0.35F, 76.0F + glow * 0.22F, 255.0F);
        }
        if (shaft) {
            const auto side_shadow = (x == 6 || x == 9) ? -18.0F : 0.0F;
            const auto center_highlight = (x == 7 || x == 8) ? 12.0F : 0.0F;
            const auto ring = shaft_ring ? 10.0F : 0.0F;
            const auto grain = ((x + y) % 4 == 0) ? 5.0F : 0.0F;
            return make_rgba(
                86.0F + side_shadow + center_highlight + ring + grain,
                58.0F + side_shadow * 0.45F + center_highlight * 0.32F + ring * 0.35F + grain * 0.4F,
                30.0F + center_highlight * 0.12F + grain * 0.25F,
                255.0F);
        }
        return make_rgba(0.0F, 0.0F, 0.0F, 0.0F);
    });
    fill_tile(pixels, 6, 3, [](int x, int y) {
        const auto head = y <= 4;
        if (head) {
            const auto edge_shadow = (x <= 1 || x >= 14 || y == 4) ? -24.0F : 0.0F;
            const auto hot_core = (x >= 4 && x <= 11 && y >= 1 && y <= 3) ? 18.0F : 0.0F;
            const auto white_core = (x >= 6 && x <= 9 && y >= 2 && y <= 3) ? 34.0F : 0.0F;
            return make_rgba(
                216.0F + hot_core + white_core * 0.65F + edge_shadow,
                170.0F + hot_core * 0.72F + white_core * 0.82F + edge_shadow * 0.30F,
                72.0F + hot_core * 0.28F + white_core * 0.55F,
                255.0F);
        }

        const auto lower_shadow = y >= 12 ? -8.0F : 0.0F;
        const auto side_shadow = (x <= 1 || x >= 14) ? -14.0F : 0.0F;
        const auto center_highlight = (x >= 7 && x <= 9) ? 10.0F : 0.0F;
        const auto strap = (y == 8 || y == 11) ? 8.0F : 0.0F;
        const auto grain = ((x + y) % 5 == 0) ? 5.0F : 0.0F;
        return make_rgba(
            90.0F + lower_shadow + side_shadow + center_highlight + strap + grain,
            60.0F + lower_shadow * 0.55F + side_shadow * 0.45F + center_highlight * 0.35F + strap * 0.30F + grain * 0.4F,
            40.0F + side_shadow * 0.18F + grain * 0.22F,
            255.0F);
    });
    fill_tile(pixels, 7, 3, [](int x, int y) {
        const auto edge = std::max(std::abs(x - 7), std::abs(y - 7));
        const auto white_core = x >= 5 && x <= 10 && y >= 5 && y <= 10;
        const auto hot_ring = x >= 3 && x <= 12 && y >= 3 && y <= 12;
        const auto outer = x >= 1 && x <= 14 && y >= 1 && y <= 14;

        if (white_core) {
            const auto center_boost = edge <= 1 ? 10.0F : 0.0F;
            return make_rgba(252.0F + center_boost, 244.0F + center_boost * 0.75F, 198.0F + center_boost * 0.45F, 255.0F);
        }
        if (hot_ring) {
            const auto edge_shadow = edge >= 4 ? -12.0F : 0.0F;
            return make_rgba(234.0F + edge_shadow, 186.0F + edge_shadow * 0.45F, 72.0F + edge_shadow * 0.18F, 255.0F);
        }
        if (outer) {
            return make_rgba(158.0F, 104.0F, 36.0F, 255.0F);
        }
        return make_rgba(66.0F, 44.0F, 24.0F, 255.0F);
    });
    fill_tile(pixels, 0, 4, [](int x, int y) {
        const auto border = x == 0 || x == 15 || y == 0 || y == 15;
        const auto cap = x >= 4 && x <= 11 && y >= 4 && y <= 11;
        const auto notch = x >= 6 && x <= 9 && y >= 6 && y <= 9;
        const auto grain = ((x + y) % 6 == 0) ? 4.0F : 0.0F;
        if (border) {
            return make_rgba(48.0F, 30.0F, 16.0F, 255.0F);
        }
        if (notch) {
            return make_rgba(60.0F, 38.0F, 18.0F, 255.0F);
        }
        if (cap) {
            return make_rgba(78.0F + grain, 50.0F + grain * 0.45F, 26.0F + grain * 0.25F, 255.0F);
        }
        return make_rgba(68.0F + grain, 44.0F + grain * 0.45F, 22.0F + grain * 0.25F, 255.0F);
    });
    fill_tile(pixels, 1, 4, [](int x, int y) {
        const auto border = x == 0 || x == 15 || y == 0 || y == 15;
        const auto mullion = x == 7 || x == 8 || y == 7 || y == 8;
        const auto pane_corner = (x <= 2 || x >= 13) && (y <= 2 || y >= 13);
        const auto frame = border || mullion;
        const auto reflection =
            std::sin((static_cast<float>(x) * 0.82F + static_cast<float>(y) * 0.34F) * 1.2F) * 7.0F +
            tile_noise(x, y, 41) * 12.0F;

        if (frame) {
            const auto wood_grain = ((x + y) % 5 == 0) ? 8.0F : 0.0F;
            return make_rgba(104.0F + wood_grain, 80.0F + wood_grain * 0.42F, 54.0F + wood_grain * 0.24F, 255.0F);
        }

        if (pane_corner) {
            return make_rgba(156.0F + reflection * 0.22F, 196.0F + reflection * 0.30F, 224.0F + reflection * 0.38F, 255.0F);
        }

        return make_rgba(0.0F, 0.0F, 0.0F, 0.0F);
    });
    fill_tile(pixels, 2, 4, [](int x, int y) {
        const auto torso = y >= 2 && y <= 13 && x >= 4 && x <= 11;
        const auto waist = y >= 10 && y <= 13 && x >= 3 && x <= 12;
        const auto neck_cut = y <= 4 && x >= 6 && x <= 9;
        const auto rib = torso && (x == 5 || x == 10 || y == 7);
        const auto highlight = torso && x <= 6 ? 18.0F : 0.0F;
        const auto shadow = torso && (x == 11 || y == 13) ? -24.0F : 0.0F;
        if (rib && !neck_cut) {
            return make_rgba(218.0F, 154.0F, 74.0F, 255.0F);
        }
        if ((torso || waist) && !neck_cut) {
            return make_rgba(174.0F + highlight + shadow, 112.0F + highlight * 0.45F + shadow * 0.42F, 52.0F + shadow * 0.20F, 255.0F);
        }
        return make_rgba(0.0F, 0.0F, 0.0F, 0.0F);
    });
    fill_tile(pixels, 3, 4, [](int x, int y) {
        const auto dx = static_cast<float>(x) - 7.5F;
        const auto dy = static_cast<float>(y) - 7.5F;
        const auto distance = std::sqrt(dx * dx + dy * dy);
        const auto rim = distance <= 7.0F && distance >= 5.6F;
        const auto face = distance < 5.6F;
        const auto boss = distance < 2.1F;
        const auto spoke = (std::abs(x - 7) <= 1 || std::abs(y - 7) <= 1) && face;
        if (rim) {
            return make_rgba(196.0F, 138.0F, 58.0F, 255.0F);
        }
        if (boss) {
            return make_rgba(230.0F, 174.0F, 86.0F, 255.0F);
        }
        if (spoke) {
            return make_rgba(132.0F, 82.0F, 38.0F, 255.0F);
        }
        if (face) {
            const auto grain = tile_noise(x, y, 211) * 22.0F;
            return make_rgba(126.0F + grain, 78.0F + grain * 0.45F, 36.0F + grain * 0.25F, 255.0F);
        }
        return make_rgba(0.0F, 0.0F, 0.0F, 0.0F);
    });
    fill_tile(pixels, 4, 4, [](int x, int y) {
        const auto blade_distance = distance_to_segment(static_cast<float>(x), static_cast<float>(y), 4.0F, 13.0F, 11.5F, 2.0F);
        const auto hilt_distance = distance_to_segment(static_cast<float>(x), static_cast<float>(y), 3.0F, 12.0F, 6.0F, 14.0F);
        const auto grip = x >= 2 && x <= 5 && y >= 12 && y <= 15;
        if (blade_distance < 1.15F && y <= 13) {
            const auto shine = blade_distance < 0.35F ? 30.0F : 0.0F;
            return make_rgba(176.0F + shine, 184.0F + shine, 186.0F + shine, 255.0F);
        }
        if (hilt_distance < 1.0F) {
            return make_rgba(202.0F, 142.0F, 54.0F, 255.0F);
        }
        if (grip) {
            return make_rgba(82.0F, 48.0F, 24.0F, 255.0F);
        }
        return make_rgba(0.0F, 0.0F, 0.0F, 0.0F);
    });
    fill_tile(pixels, 5, 4, [](int x, int y) {
        const auto shaft_distance = distance_to_segment(static_cast<float>(x), static_cast<float>(y), 3.0F, 14.0F, 12.0F, 3.0F);
        const auto spear_head = y <= 5 && x >= 9 && x <= 14 && (x + y >= 13);
        const auto butt = x <= 4 && y >= 12;
        if (spear_head) {
            const auto edge = x >= 13 || y <= 1 ? 24.0F : 0.0F;
            return make_rgba(166.0F + edge, 174.0F + edge, 176.0F + edge, 255.0F);
        }
        if (shaft_distance < 0.85F) {
            return make_rgba(118.0F, 74.0F, 34.0F, 255.0F);
        }
        if (butt) {
            return make_rgba(188.0F, 132.0F, 54.0F, 255.0F);
        }
        return make_rgba(0.0F, 0.0F, 0.0F, 0.0F);
    });
    fill_tile(pixels, 6, 4, [](int x, int y) {
        const auto left_sole = y >= 10 && y <= 13 && x >= 2 && x <= 7;
        const auto right_sole = y >= 9 && y <= 12 && x >= 8 && x <= 13;
        const auto left_strap = left_sole && (x == 4 || y == 11);
        const auto right_strap = right_sole && (x == 10 || y == 10);
        if (left_strap || right_strap) {
            return make_rgba(92.0F, 50.0F, 26.0F, 255.0F);
        }
        if (left_sole || right_sole) {
            const auto grain = tile_noise(x, y, 223) * 12.0F;
            return make_rgba(142.0F + grain, 86.0F + grain * 0.45F, 42.0F + grain * 0.24F, 255.0F);
        }
        return make_rgba(0.0F, 0.0F, 0.0F, 0.0F);
    });
    fill_tile(pixels, 7, 4, [](int x, int y) {
        const auto belt = y >= 2 && y <= 4 && x >= 4 && x <= 11;
        const auto left_leg = y >= 5 && y <= 14 && x >= 4 && x <= 7;
        const auto right_leg = y >= 5 && y <= 14 && x >= 8 && x <= 11;
        const auto seam = y >= 5 && (x == 7 || x == 8);
        if (belt) {
            return make_rgba(112.0F, 62.0F, 28.0F, 255.0F);
        }
        if (seam) {
            return make_rgba(58.0F, 72.0F, 88.0F, 255.0F);
        }
        if (left_leg || right_leg) {
            const auto cloth = tile_noise(x, y, 229) * 18.0F;
            return make_rgba(78.0F + cloth * 0.25F, 92.0F + cloth * 0.55F, 112.0F + cloth, 255.0F);
        }
        return make_rgba(0.0F, 0.0F, 0.0F, 0.0F);
    });
    fill_tile(pixels, 1, 3, [](int x, int y) {
        const auto stem = ((x == 7) || (x == 8)) && y > 5;
        const auto blade_a = y > 4 && x >= 3 && x <= 6 && x <= y;
        const auto blade_b = y > 2 && x >= 9 && x <= 12 && (15 - x) <= y + 1;
        const auto blade_c = y > 7 && x >= 5 && x <= 9 && std::abs(x - 7) <= (15 - y) / 2;
        const auto alpha = stem || blade_a || blade_b || blade_c ? 255.0F : 0.0F;
        const auto noise = tile_noise(x, y, 21);
        const auto highlight = y < 5 ? 8.0F : 0.0F;
        return make_rgba(48.0F + noise * 10.0F, 118.0F + noise * 34.0F + highlight, 42.0F + noise * 10.0F, alpha);
    });
    fill_tile(pixels, 2, 3, [](int x, int y) {
        const auto stem = (x == 7 || x == 8) && y > 7;
        const auto petal = radial_falloff(static_cast<float>(x), static_cast<float>(y), 7.5F, 5.5F, 4.0F);
        const auto center = radial_falloff(static_cast<float>(x), static_cast<float>(y), 7.5F, 5.5F, 1.6F);
        if (center > 0.52F) {
            return make_rgba(224.0F, 186.0F, 58.0F, 255.0F);
        }
        if (petal > 0.30F) {
            return make_rgba(190.0F + petal * 34.0F, 52.0F + petal * 22.0F, 66.0F + petal * 26.0F, 255.0F);
        }
        if (stem) {
            return make_rgba(72.0F, 142.0F, 56.0F, 255.0F);
        }
        return make_rgba(0.0F, 0.0F, 0.0F, 0.0F);
    });
    fill_tile(pixels, 3, 3, [](int x, int y) {
        const auto stem = (x == 7 || x == 8) && y > 7;
        const auto petal = radial_falloff(static_cast<float>(x), static_cast<float>(y), 7.5F, 5.0F, 4.1F);
        const auto center = radial_falloff(static_cast<float>(x), static_cast<float>(y), 7.5F, 5.0F, 1.5F);
        if (center > 0.50F) {
            return make_rgba(222.0F, 166.0F, 38.0F, 255.0F);
        }
        if (petal > 0.28F) {
            return make_rgba(234.0F + petal * 18.0F, 201.0F + petal * 20.0F, 56.0F + petal * 16.0F, 255.0F);
        }
        if (stem) {
            return make_rgba(70.0F, 138.0F, 54.0F, 255.0F);
        }
        return make_rgba(0.0F, 0.0F, 0.0F, 0.0F);
    });
    fill_tile(pixels, 4, 3, [](int x, int y) {
        const auto branch_a = std::abs(x - 7) <= 1 && y > 4;
        const auto branch_b = y > 5 && y < 11 && x >= y - 1 && x <= y + 1;
        const auto branch_c = y > 6 && x >= (14 - y) && x <= (16 - y);
        const auto alpha = branch_a || branch_b || branch_c ? 255.0F : 0.0F;
        const auto noise = tile_noise(x, y, 22);
        return make_rgba(118.0F + noise * 18.0F, 88.0F + noise * 12.0F, 52.0F + noise * 8.0F, alpha);
    });
    fill_tile(pixels, 5, 3, [](int x, int y) {
        const auto sample_x = x == kBlockAtlasTileSize - 1 ? 0 : x;
        const auto sample_y = y == kBlockAtlasTileSize - 1 ? 0 : y;
        const auto phase_x = static_cast<float>(sample_x) / kTileEdgeSpan;
        const auto phase_y = static_cast<float>(sample_y) / kTileEdgeSpan;
        const auto ripple_a =
            0.5F + 0.5F * std::sin((phase_x + phase_y * 2.0F) * kFullTurnRadians);
        const auto ripple_b =
            0.5F + 0.5F * std::cos((phase_x * 2.0F - phase_y) * kFullTurnRadians);
        const auto ripple_c =
            0.5F + 0.5F * std::sin((phase_x * 3.0F + phase_y * 3.0F) * kFullTurnRadians);
        const auto highlight = ripple_a * 0.38F + ripple_b * 0.34F + ripple_c * 0.28F;
        const auto depth =
            0.5F + 0.5F * std::cos((phase_x * 2.0F + phase_y * 2.0F) * kFullTurnRadians);
        const auto caustic =
            0.5F + 0.5F * std::sin((phase_x * 4.0F - phase_y * 4.0F) * kFullTurnRadians);
        return make_rgba(
            26.0F + highlight * 5.0F + caustic * 2.0F,
            104.0F + highlight * 12.0F + depth * 6.0F,
            170.0F + highlight * 15.0F + depth * 8.0F + caustic * 4.0F,
            184.0F);
    });

    for (int stage = 0; stage < static_cast<int>(kBlockBreakStageCount); ++stage) {
        fill_tile(pixels, stage, kBlockBreakCrackAtlasRow, [stage](int x, int y) {
            const auto strength = crack_strength(x, y, stage);
            if (strength <= 0.40F) {
                return make_rgba(0.0F, 0.0F, 0.0F, 0.0F);
            }

            // Je garde des fissures bien contrastees avec un peu de matiere autour
            // pour que la casse reste lisible meme dans les zones ombrees.
            const auto soot = 12.0F + (1.0F - strength) * 38.0F + tile_noise(x, y, 131 + stage) * 10.0F;
            const auto rim = strength > 0.86F ? 10.0F : 0.0F;
            return make_rgba(soot + rim * 0.20F, soot + 3.0F + rim * 0.14F, soot + 6.0F + rim * 0.08F, 255.0F);
        });
    }

    return pixels;
}

auto build_accent_atlas_pixels() -> std::vector<std::uint8_t> {
    std::vector<std::uint8_t> pixels(static_cast<std::size_t>(kAccentAtlasSize * kAccentAtlasSize * 4), 0);

    fill_tile(pixels, kAccentAtlasSize, kAccentAtlasTileSize, 0, 0, [](int x, int y) {
        const auto outer = radial_falloff(static_cast<float>(x), static_cast<float>(y), 7.5F, 7.5F, 7.6F);
        const auto disc = radial_falloff(static_cast<float>(x), static_cast<float>(y), 7.5F, 7.5F, 5.2F);
        const auto core = radial_falloff(static_cast<float>(x), static_cast<float>(y), 7.1F, 7.0F, 3.1F);
        const auto corona = saturate(outer * 0.78F + disc * 0.40F);
        const auto alpha = saturate(outer * 0.72F + disc * 0.45F);
        return make_rgba(
            236.0F + disc * 18.0F + core * 14.0F,
            182.0F + corona * 54.0F + core * 10.0F,
            88.0F + corona * 24.0F,
            alpha * 255.0F);
    });
    fill_tile(pixels, kAccentAtlasSize, kAccentAtlasTileSize, 1, 0, [](int x, int y) {
        const auto disc = radial_falloff(static_cast<float>(x), static_cast<float>(y), 7.5F, 7.5F, 6.6F);
        const auto crater_a = radial_falloff(static_cast<float>(x), static_cast<float>(y), 5.4F, 6.3F, 1.7F);
        const auto crater_b = radial_falloff(static_cast<float>(x), static_cast<float>(y), 10.1F, 9.0F, 1.5F);
        const auto crater_c = radial_falloff(static_cast<float>(x), static_cast<float>(y), 8.6F, 4.1F, 1.2F);
        const auto limb = saturate((9.5F - static_cast<float>(x)) / 9.5F);
        const auto crater_shadow = crater_a * 16.0F + crater_b * 12.0F + crater_c * 9.0F;
        return make_rgba(
            204.0F + disc * 18.0F + limb * 8.0F - crater_shadow,
            216.0F + disc * 18.0F + limb * 6.0F - crater_shadow * 0.7F,
            238.0F + disc * 14.0F + limb * 4.0F - crater_shadow * 0.4F,
            disc * 255.0F);
    });
    fill_tile(pixels, kAccentAtlasSize, kAccentAtlasTileSize, 2, 0, [](int x, int y) {
        const auto center = radial_falloff(static_cast<float>(x), static_cast<float>(y), 7.5F, 7.5F, 1.9F);
        const auto vertical = x >= 7 && x <= 8 ? radial_falloff(7.5F, static_cast<float>(y), 7.5F, 7.5F, 7.0F) : 0.0F;
        const auto horizontal =
            y >= 7 && y <= 8 ? radial_falloff(static_cast<float>(x), 7.5F, 7.5F, 7.5F, 7.0F) : 0.0F;
        const auto diagonal_a = std::abs(x - y) <= 1 ? 0.60F - std::abs(static_cast<float>(x) - 7.5F) * 0.08F : 0.0F;
        const auto diagonal_b = std::abs((x + y) - 15) <= 1 ? 0.60F - std::abs(static_cast<float>(x) - 7.5F) * 0.08F : 0.0F;
        const auto sparkle = saturate(std::max(std::max(center, vertical), std::max(horizontal, std::max(diagonal_a, diagonal_b))));
        return make_rgba(
            228.0F + sparkle * 24.0F,
            236.0F + sparkle * 16.0F,
            248.0F + sparkle * 7.0F,
            sparkle * 255.0F);
    });
    fill_tile(pixels, kAccentAtlasSize, kAccentAtlasTileSize, 3, 0, [](int x, int y) {
        const auto puff_a = radial_falloff(static_cast<float>(x), static_cast<float>(y), 4.4F, 8.6F, 4.7F);
        const auto puff_b = radial_falloff(static_cast<float>(x), static_cast<float>(y), 8.2F, 5.5F, 4.5F);
        const auto puff_c = radial_falloff(static_cast<float>(x), static_cast<float>(y), 11.8F, 8.1F, 4.4F);
        const auto puff_d = radial_falloff(static_cast<float>(x), static_cast<float>(y), 8.6F, 9.4F, 3.8F);
        const auto trailing = radial_falloff(static_cast<float>(x), static_cast<float>(y), 2.6F, 9.4F, 3.2F);
        const auto notch = radial_falloff(static_cast<float>(x), static_cast<float>(y), 8.4F, 6.1F, 1.8F);
        const auto cloud = saturate(std::max({puff_a, puff_b, puff_c, puff_d, trailing * 0.72F}) * 0.96F - notch * 0.24F);
        const auto alpha = saturate(cloud * 0.82F + trailing * 0.08F);
        return make_rgba(
            226.0F + cloud * 18.0F,
            232.0F + cloud * 18.0F,
            244.0F + cloud * 10.0F,
            alpha * 196.0F);
    });
    fill_tile(pixels, kAccentAtlasSize, kAccentAtlasTileSize, 0, 1, [](int x, int y) {
        const auto outer = radial_falloff(static_cast<float>(x), static_cast<float>(y), 7.5F, 7.5F, 7.2F);
        const auto mid = radial_falloff(static_cast<float>(x), static_cast<float>(y), 7.5F, 7.5F, 6.0F);
        const auto inner = radial_falloff(static_cast<float>(x), static_cast<float>(y), 7.5F, 7.5F, 4.5F);
        const auto ring = saturate((outer - mid * 0.92F) + (mid - inner * 0.98F) * 0.30F);
        return make_rgba(
            124.0F + ring * 78.0F,
            214.0F + ring * 28.0F,
            228.0F + ring * 20.0F,
            ring * 224.0F);
    });
    fill_tile(pixels, kAccentAtlasSize, kAccentAtlasTileSize, 1, 1, [](int x, int y) {
        const auto vertical = x >= 7 && x <= 8 ? radial_falloff(7.5F, static_cast<float>(y), 7.5F, 7.5F, 5.6F) : 0.0F;
        const auto horizontal =
            y >= 7 && y <= 8 ? radial_falloff(static_cast<float>(x), 7.5F, 7.5F, 7.5F, 5.6F) : 0.0F;
        const auto diagonal_a = std::abs(x - y) <= 1 ? 0.85F - std::abs(static_cast<float>(x) - 7.5F) * 0.08F : 0.0F;
        const auto diagonal_b = std::abs((x + y) - 15) <= 1 ? 0.85F - std::abs(static_cast<float>(x) - 7.5F) * 0.08F : 0.0F;
        const auto spark = saturate(std::max(std::max(vertical, horizontal), std::max(diagonal_a, diagonal_b)));
        return make_rgba(
            255.0F,
            220.0F + spark * 20.0F,
            164.0F + spark * 26.0F,
            spark * 245.0F);
    });

    return pixels;
}

} // namespace valcraft
