#include "world/BlockVisuals.h"

#include <algorithm>
#include <array>
#include <cmath>

namespace valcraft {

namespace {

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

auto bark_value(int x, int y, int seed, float base) noexcept -> float {
    const auto stripe = std::sin((static_cast<float>(x) + tile_noise(x, y, seed) * 2.3F) * 1.3F);
    const auto fiber = tile_noise(x, y, seed + 7) * 16.0F;
    return base + stripe * 8.0F + fiber;
}

} // namespace

auto build_block_atlas_pixels() -> std::vector<std::uint8_t> {
    std::vector<std::uint8_t> pixels(static_cast<std::size_t>(kBlockAtlasSize * kBlockAtlasSize * 4), 0);

    fill_tile(pixels, 0, 0, [](int x, int y) {
        const auto noise = tile_noise(x, y, 1);
        const auto flower = radial_falloff(static_cast<float>(x), static_cast<float>(y), 4.5F, 5.0F, 2.2F) * 30.0F;
        const auto bright = radial_falloff(static_cast<float>(x), static_cast<float>(y), 11.0F, 10.0F, 2.6F) * 24.0F;
        return make_rgba(
            52.0F + noise * 16.0F + bright * 0.08F,
            116.0F + noise * 38.0F + flower * 0.55F + bright * 0.80F,
            36.0F + noise * 12.0F + flower * 0.05F,
            255.0F);
    });
    fill_tile(pixels, 1, 0, [](int x, int y) {
        if (y < 4) {
            const auto dew = ((x + y) % 5 == 0) ? 12.0F : 0.0F;
            return make_rgba(56.0F, 146.0F + dew, 46.0F, 255.0F);
        }
        const auto soil = tile_noise(x, y, 2);
        const auto roots = (x % 5 == 0 && y > 6) ? 18.0F : 0.0F;
        return make_rgba(
            108.0F + soil * 26.0F,
            78.0F + soil * 14.0F + roots * 0.15F,
            44.0F + soil * 10.0F,
            255.0F);
    });
    fill_tile(pixels, 2, 0, [](int x, int y) {
        const auto soil = tile_noise(x, y, 3);
        return make_rgba(101.0F + soil * 30.0F, 71.0F + soil * 19.0F, 43.0F + soil * 12.0F, 255.0F);
    });
    fill_tile(pixels, 3, 0, [](int x, int y) {
        const auto stone = tile_noise(x, y, 4);
        const auto crack = (std::abs(x - y) <= 1 || std::abs((x + y) - 14) <= 1) ? 18.0F : 0.0F;
        return make_rgba(
            98.0F + stone * 36.0F - crack,
            101.0F + stone * 35.0F - crack,
            106.0F + stone * 38.0F - crack * 0.5F,
            255.0F);
    });
    fill_tile(pixels, 4, 0, [](int x, int y) {
        const auto grain = tile_noise(x, y, 5);
        const auto dune = std::sin((static_cast<float>(x) + static_cast<float>(y) * 0.5F) * 0.55F) * 5.0F;
        return make_rgba(
            198.0F + grain * 25.0F + dune,
            183.0F + grain * 18.0F + dune * 0.6F,
            126.0F + grain * 16.0F,
            255.0F);
    });
    fill_tile(pixels, 5, 0, [](int x, int y) {
        const auto cell_x = x / 4;
        const auto cell_y = y / 4;
        const auto mortar = (x % 4 == 0 || y % 4 == 0) ? 26.0F : 0.0F;
        const auto stone = tile_noise(cell_x, cell_y, 6) * 32.0F;
        return make_rgba(104.0F + stone - mortar, 107.0F + stone - mortar, 112.0F + stone - mortar * 0.6F, 255.0F);
    });
    fill_tile(pixels, 6, 0, [](int x, int y) {
        const auto pebble = radial_falloff(static_cast<float>(x), static_cast<float>(y), 4.0F, 5.0F, 3.0F) * 22.0F;
        const auto pebble_b = radial_falloff(static_cast<float>(x), static_cast<float>(y), 11.0F, 10.0F, 3.2F) * 18.0F;
        const auto grain = tile_noise(x, y, 7);
        return make_rgba(
            116.0F + grain * 34.0F + pebble - pebble_b * 0.35F,
            113.0F + grain * 28.0F + pebble_b * 0.45F,
            108.0F + grain * 24.0F,
            255.0F);
    });
    fill_tile(pixels, 7, 0, [](int x, int y) {
        const auto stone = tile_noise(x, y, 8);
        const auto moss = saturate(tile_noise(x + 3, y + 5, 9) * 1.25F - 0.42F);
        return make_rgba(
            90.0F + stone * 30.0F - moss * 18.0F,
            97.0F + stone * 28.0F + moss * 44.0F,
            94.0F + stone * 31.0F - moss * 12.0F,
            255.0F);
    });

    fill_tile(pixels, 0, 1, [](int x, int y) {
        const auto bark = bark_value(x, y, 10, 82.0F);
        const auto knot = radial_falloff(static_cast<float>(x), static_cast<float>(y), 5.0F, 10.5F, 2.1F) * 10.0F;
        const auto highlight = ((x + y) % 5 == 0) ? 4.0F : 0.0F;
        return make_rgba(
            bark + 30.0F - knot * 0.25F + highlight,
            bark + 10.0F - knot * 0.45F + highlight * 0.6F,
            bark - 22.0F - knot * 0.7F,
            255.0F);
    });
    fill_tile(pixels, 1, 1, [](int x, int y) {
        const auto dx = static_cast<float>(x) - 7.5F;
        const auto dy = static_cast<float>(y) - 7.5F;
        const auto rings = std::sin(std::sqrt(dx * dx + dy * dy) * 1.8F) * 7.0F;
        const auto grain = tile_noise(x, y, 11) * 14.0F;
        return make_rgba(150.0F + rings + grain, 111.0F + rings * 0.7F + grain * 0.7F, 70.0F + rings * 0.5F, 255.0F);
    });
    fill_tile(pixels, 2, 1, [](int x, int y) {
        const auto plank_band = ((y / 5) % 2 == 0) ? 0.0F : -12.0F;
        const auto seam = (y == 5 || y == 10) ? 24.0F : 0.0F;
        const auto knot = radial_falloff(static_cast<float>(x), static_cast<float>(y), 4.0F, 11.0F, 2.4F) * 14.0F;
        const auto grain = tile_noise(x, y, 12) * 12.0F;
        return make_rgba(
            150.0F + plank_band + grain - seam - knot * 0.35F,
            113.0F + plank_band * 0.8F + grain * 0.8F - seam - knot * 0.28F,
            70.0F + grain * 0.5F - knot * 0.12F,
            255.0F);
    });
    fill_tile(pixels, 3, 1, [](int x, int y) {
        const auto leaf = tile_noise(x, y, 13);
        const auto lobe_a = radial_falloff(static_cast<float>(x), static_cast<float>(y), 4.6F, 5.2F, 6.3F);
        const auto lobe_b = radial_falloff(static_cast<float>(x), static_cast<float>(y), 11.1F, 5.7F, 6.1F);
        const auto lobe_c = radial_falloff(static_cast<float>(x), static_cast<float>(y), 7.8F, 11.1F, 6.4F);
        const auto canopy = std::max({lobe_a, lobe_b * 0.96F, lobe_c * 0.92F});
        const auto notch =
            ((x < 2 && y < 4) || (x > 13 && y < 3) || (x < 3 && y > 12) || (x > 12 && y > 12)) ? 0.18F : 0.0F;
        const auto shell = canopy - notch + leaf * 0.18F;
        const auto branch = ((std::abs(x - 7) <= 1) && y > 7) ||
                            (y >= 7 && y <= 9 && x >= 4 && x <= 11 && ((x + y) % 3 != 0));
        const auto alpha = branch || shell > 0.38F ? 255.0F : 0.0F;
        const auto top_light = 1.0F - static_cast<float>(y) / 15.0F;
        const auto shadow = std::abs(x - 8) > 4 ? 6.0F : 0.0F;
        return make_rgba(
            46.0F + leaf * 16.0F + top_light * 10.0F - shadow * 0.25F,
            92.0F + leaf * 42.0F + top_light * 22.0F - shadow,
            34.0F + leaf * 14.0F + top_light * 4.0F,
            alpha);
    });
    fill_tile(pixels, 4, 1, [](int x, int y) {
        const auto bark = bark_value(x, y, 14, 72.0F);
        const auto resin = radial_falloff(static_cast<float>(x), static_cast<float>(y), 10.5F, 4.0F, 1.9F) * 8.0F;
        return make_rgba(bark + 20.0F + resin, bark + 8.0F + resin * 0.5F, bark - 18.0F - resin * 0.35F, 255.0F);
    });
    fill_tile(pixels, 5, 1, [](int x, int y) {
        const auto dx = static_cast<float>(x) - 7.5F;
        const auto dy = static_cast<float>(y) - 7.5F;
        const auto rings = std::sin(std::sqrt(dx * dx + dy * dy) * 1.7F) * 6.0F;
        const auto grain = tile_noise(x, y, 15) * 10.0F;
        return make_rgba(128.0F + rings + grain, 100.0F + rings * 0.8F, 72.0F + rings * 0.5F, 255.0F);
    });
    fill_tile(pixels, 6, 1, [](int x, int y) {
        const auto leaf = tile_noise(x, y, 16);
        float half_width = 3.0F;
        if (y >= 3 && y < 6) {
            half_width = 4.8F;
        } else if (y >= 6 && y < 10) {
            half_width = 6.6F;
        } else if (y >= 10 && y < 13) {
            half_width = 5.4F;
        } else if (y >= 13) {
            half_width = 3.8F;
        }
        const auto profile = 1.0F - std::abs(static_cast<float>(x) - 7.5F) / std::max(half_width, 0.001F);
        const auto twig = (std::abs(x - 7) <= 1) && y > 2;
        const auto edge_hole = (profile < 0.18F) && (((x + y) % 3) == 0);
        const auto alpha = twig || (profile > 0.08F && leaf > 0.10F && !edge_hole) ? 255.0F : 0.0F;
        const auto top_light = 1.0F - static_cast<float>(y) / 15.0F;
        return make_rgba(
            30.0F + leaf * 14.0F + top_light * 6.0F,
            74.0F + leaf * 32.0F + top_light * 15.0F,
            32.0F + leaf * 12.0F + top_light * 4.0F,
            alpha);
    });

    fill_tile(pixels, 0, 2, [](int x, int y) {
        const auto snow = tile_noise(x, y, 17);
        const auto sparkle = ((x + y) % 7 == 0) ? 10.0F : 0.0F;
        return make_rgba(224.0F + snow * 26.0F + sparkle, 232.0F + snow * 20.0F + sparkle, 239.0F + snow * 18.0F, 255.0F);
    });
    fill_tile(pixels, 1, 2, [](int x, int y) {
        if (y < 6) {
            const auto snow = tile_noise(x, y, 18);
            return make_rgba(221.0F + snow * 18.0F, 229.0F + snow * 15.0F, 236.0F + snow * 14.0F, 255.0F);
        }
        const auto soil = tile_noise(x, y, 19);
        return make_rgba(106.0F + soil * 26.0F, 77.0F + soil * 16.0F, 43.0F + soil * 10.0F, 255.0F);
    });
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
        const auto flame = (y < 5 && x > 4 && x < 11) ? 52.0F : 0.0F;
        const auto ember = (y < 3 && x > 5 && x < 10) ? 36.0F : 0.0F;
        const auto shaft = (x > 6 && x < 10 && y > 3) ? 1.0F : 0.0F;
        return make_rgba(
            shaft > 0.5F ? 140.0F + flame : 22.0F + flame + ember,
            shaft > 0.5F ? 98.0F + flame * 0.4F : 18.0F + flame * 0.8F,
            shaft > 0.5F ? 54.0F : 14.0F,
            shaft > 0.5F || flame > 0.0F ? 255.0F : 0.0F);
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

    return pixels;
}

auto build_accent_atlas_pixels() -> std::vector<std::uint8_t> {
    std::vector<std::uint8_t> pixels(static_cast<std::size_t>(kAccentAtlasSize * kAccentAtlasSize * 4), 0);

    fill_tile(pixels, kAccentAtlasSize, kAccentAtlasTileSize, 0, 0, [](int x, int y) {
        const auto glow = radial_falloff(static_cast<float>(x), static_cast<float>(y), 7.5F, 7.5F, 7.4F);
        const auto core = radial_falloff(static_cast<float>(x), static_cast<float>(y), 7.5F, 7.5F, 4.4F);
        const auto alpha = saturate(glow * 0.95F + core * 0.45F);
        return make_rgba(
            235.0F + core * 18.0F,
            176.0F + glow * 56.0F,
            78.0F + glow * 24.0F,
            alpha * 255.0F);
    });
    fill_tile(pixels, kAccentAtlasSize, kAccentAtlasTileSize, 1, 0, [](int x, int y) {
        const auto disc = radial_falloff(static_cast<float>(x), static_cast<float>(y), 7.5F, 7.5F, 6.6F);
        const auto crater_a = radial_falloff(static_cast<float>(x), static_cast<float>(y), 5.5F, 6.5F, 1.8F);
        const auto crater_b = radial_falloff(static_cast<float>(x), static_cast<float>(y), 10.3F, 9.2F, 1.6F);
        const auto crater_c = radial_falloff(static_cast<float>(x), static_cast<float>(y), 8.8F, 4.3F, 1.3F);
        const auto crater_shadow = crater_a * 18.0F + crater_b * 14.0F + crater_c * 10.0F;
        return make_rgba(
            206.0F + disc * 18.0F - crater_shadow,
            220.0F + disc * 16.0F - crater_shadow * 0.7F,
            246.0F + disc * 8.0F - crater_shadow * 0.4F,
            disc * 255.0F);
    });
    fill_tile(pixels, kAccentAtlasSize, kAccentAtlasTileSize, 2, 0, [](int x, int y) {
        const auto center = radial_falloff(static_cast<float>(x), static_cast<float>(y), 7.5F, 7.5F, 1.9F);
        const auto vertical = x >= 7 && x <= 8 ? radial_falloff(7.5F, static_cast<float>(y), 7.5F, 7.5F, 7.0F) : 0.0F;
        const auto horizontal =
            y >= 7 && y <= 8 ? radial_falloff(static_cast<float>(x), 7.5F, 7.5F, 7.5F, 7.0F) : 0.0F;
        const auto sparkle = std::max(center, std::max(vertical, horizontal));
        return make_rgba(
            232.0F + sparkle * 20.0F,
            238.0F + sparkle * 14.0F,
            255.0F,
            sparkle * 255.0F);
    });
    fill_tile(pixels, kAccentAtlasSize, kAccentAtlasTileSize, 3, 0, [](int x, int y) {
        const auto puff_a = radial_falloff(static_cast<float>(x), static_cast<float>(y), 4.6F, 8.2F, 4.9F);
        const auto puff_b = radial_falloff(static_cast<float>(x), static_cast<float>(y), 8.0F, 5.6F, 4.7F);
        const auto puff_c = radial_falloff(static_cast<float>(x), static_cast<float>(y), 11.4F, 8.4F, 4.6F);
        const auto cloud = saturate(std::max({puff_a, puff_b, puff_c}) * 0.95F);
        return make_rgba(
            234.0F + cloud * 10.0F,
            238.0F + cloud * 12.0F,
            248.0F + cloud * 7.0F,
            cloud * 190.0F);
    });
    fill_tile(pixels, kAccentAtlasSize, kAccentAtlasTileSize, 0, 1, [](int x, int y) {
        const auto outer = radial_falloff(static_cast<float>(x), static_cast<float>(y), 7.5F, 7.5F, 7.2F);
        const auto inner = radial_falloff(static_cast<float>(x), static_cast<float>(y), 7.5F, 7.5F, 5.4F);
        const auto ring = saturate(outer - inner * 0.96F);
        return make_rgba(
            100.0F + ring * 70.0F,
            226.0F + ring * 16.0F,
            232.0F + ring * 14.0F,
            ring * 235.0F);
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
