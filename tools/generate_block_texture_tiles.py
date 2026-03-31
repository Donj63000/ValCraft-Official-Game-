#!/usr/bin/env python3
from __future__ import annotations

import argparse
import colorsys
import math
import sys
from pathlib import Path
from typing import Dict, Iterable

try:
    from PIL import Image, ImageEnhance
except ImportError as exc:  # pragma: no cover - utility script
    sys.stderr.write(
        "This script requires Pillow. Install it with: python -m pip install Pillow\n"
    )
    raise SystemExit(1) from exc

TILE_SIZE = 16
PACKED_TILE_PIXEL_COUNT = TILE_SIZE * TILE_SIZE
PROJECT_ROOT = Path(__file__).resolve().parents[1]
DEFAULT_TEXTURE_DIR = PROJECT_ROOT / "Textures"
DEFAULT_OUTPUT = PROJECT_ROOT / "src" / "world" / "GeneratedBlockTextureTiles.h"

PRIMARY_TEXTURES = {
    "stone": "Roche.png",
    "dirt": "Terre.png",
    "grass": "herbe.png",
    "snow": "neige.png",
    "sand": "sable.png",
    "bark": "troncarbre.png",
    "leaves": "feuilles.png",
}

OUTPUT_ORDER = (
    ("kGrassTopTile", "grass_top"),
    ("kGrassSideTile", "grass_side"),
    ("kDirtTile", "dirt"),
    ("kStoneTile", "stone"),
    ("kSandTile", "sand"),
    ("kCobblestoneTile", "cobblestone"),
    ("kGravelTile", "gravel"),
    ("kMossyStoneTile", "mossy_stone"),
    ("kWoodSideTile", "wood_side"),
    ("kWoodTopTile", "wood_top"),
    ("kPlanksTile", "planks"),
    ("kLeavesTile", "leaves"),
    ("kPineWoodSideTile", "pine_wood_side"),
    ("kPineWoodTopTile", "pine_wood_top"),
    ("kPineLeavesTile", "pine_leaves"),
    ("kSnowTopTile", "snow_top"),
    ("kSnowSideTile", "snow_side"),
)

ATLAS_COORDS = (
    (0, 0),
    (1, 0),
    (2, 0),
    (3, 0),
    (4, 0),
    (5, 0),
    (6, 0),
    (7, 0),
    (0, 1),
    (1, 1),
    (2, 1),
    (3, 1),
    (4, 1),
    (5, 1),
    (6, 1),
    (0, 2),
    (1, 2),
)


def _clamp_byte(value: float) -> int:
    return max(0, min(255, int(round(value))))


def _pack_rgba(pixel: tuple[int, int, int, int]) -> int:
    red, green, blue, alpha = pixel
    return (red << 24) | (green << 16) | (blue << 8) | alpha


def _load_primary_tile(
    texture_dir: Path,
    filename: str,
    *,
    saturation: float = 1.0,
    contrast: float = 1.0,
    brightness: float = 1.0,
) -> Image.Image:
    path = texture_dir / filename
    image = Image.open(path).convert("RGBA")
    width, height = image.size
    crop_size = min(width, height)
    crop_left = (width - crop_size) // 2
    crop_top = (height - crop_size) // 2
    tile = image.crop((crop_left, crop_top, crop_left + crop_size, crop_top + crop_size))
    tile = tile.resize((TILE_SIZE, TILE_SIZE), Image.Resampling.LANCZOS)

    if brightness != 1.0:
        tile = ImageEnhance.Brightness(tile).enhance(brightness)
    if contrast != 1.0:
        tile = ImageEnhance.Contrast(tile).enhance(contrast)
    if saturation != 1.0:
        tile = ImageEnhance.Color(tile).enhance(saturation)

    pixels = tile.load()
    for y in range(TILE_SIZE):
        edge_left = pixels[0, y]
        edge_right = pixels[TILE_SIZE - 1, y]
        average = tuple((edge_left[channel] + edge_right[channel]) // 2 for channel in range(4))
        pixels[0, y] = average
        pixels[TILE_SIZE - 1, y] = average

        inner_left = pixels[1, y]
        inner_right = pixels[TILE_SIZE - 2, y]
        pixels[1, y] = tuple(
            _clamp_byte(inner_left[channel] * 0.65 + inner_right[channel] * 0.35) for channel in range(4)
        )
        pixels[TILE_SIZE - 2, y] = tuple(
            _clamp_byte(inner_right[channel] * 0.65 + inner_left[channel] * 0.35) for channel in range(4)
        )

    for x in range(TILE_SIZE):
        edge_top = pixels[x, 0]
        edge_bottom = pixels[x, TILE_SIZE - 1]
        average = tuple((edge_top[channel] + edge_bottom[channel]) // 2 for channel in range(4))
        pixels[x, 0] = average
        pixels[x, TILE_SIZE - 1] = average

        inner_top = pixels[x, 1]
        inner_bottom = pixels[x, TILE_SIZE - 2]
        pixels[x, 1] = tuple(
            _clamp_byte(inner_top[channel] * 0.65 + inner_bottom[channel] * 0.35) for channel in range(4)
        )
        pixels[x, TILE_SIZE - 2] = tuple(
            _clamp_byte(inner_bottom[channel] * 0.65 + inner_top[channel] * 0.35) for channel in range(4)
        )

    return tile


def _average_rgb(image: Image.Image) -> tuple[float, float, float]:
    pixels = image.load()
    red_sum = 0
    green_sum = 0
    blue_sum = 0
    for y in range(TILE_SIZE):
        for x in range(TILE_SIZE):
            red, green, blue, _ = pixels[x, y]
            red_sum += red
            green_sum += green
            blue_sum += blue
    count = float(PACKED_TILE_PIXEL_COUNT)
    return red_sum / count, green_sum / count, blue_sum / count


def _luma(pixel: tuple[int, int, int, int]) -> float:
    red, green, blue, _ = pixel
    return red * 0.2126 + green * 0.7152 + blue * 0.0722


def _blend(lhs: tuple[int, int, int, int], rhs: tuple[int, int, int, int], alpha: float) -> tuple[int, int, int, int]:
    return tuple(_clamp_byte(lhs[index] * (1.0 - alpha) + rhs[index] * alpha) for index in range(4))


def _composite_top(bottom: Image.Image, top: Image.Image, *, top_rows: int, blend_rows: int = 1) -> Image.Image:
    result = bottom.copy()
    output = result.load()
    bottom_pixels = bottom.load()
    top_pixels = top.load()

    for y in range(TILE_SIZE):
        if y < top_rows:
            alpha = 1.0
        elif y < top_rows + blend_rows:
            alpha = 1.0 - (y - top_rows + 1) / float(blend_rows + 1)
        else:
            alpha = 0.0

        if alpha <= 0.0:
            continue

        for x in range(TILE_SIZE):
            output[x, y] = _blend(bottom_pixels[x, y], top_pixels[x, y], alpha)

    return result


def _tint(
    image: Image.Image,
    *,
    multiply: tuple[float, float, float],
    add: tuple[float, float, float],
    saturation: float,
) -> Image.Image:
    source = image.load()
    result = Image.new("RGBA", (TILE_SIZE, TILE_SIZE))
    output = result.load()

    for y in range(TILE_SIZE):
        for x in range(TILE_SIZE):
            red, green, blue, alpha = source[x, y]
            red = _clamp_byte(red * multiply[0] + add[0])
            green = _clamp_byte(green * multiply[1] + add[1])
            blue = _clamp_byte(blue * multiply[2] + add[2])
            hue, lightness, sat = colorsys.rgb_to_hls(red / 255.0, green / 255.0, blue / 255.0)
            sat = max(0.0, min(1.0, sat * saturation))
            out_red, out_green, out_blue = colorsys.hls_to_rgb(hue, lightness, sat)
            output[x, y] = (
                _clamp_byte(out_red * 255.0),
                _clamp_byte(out_green * 255.0),
                _clamp_byte(out_blue * 255.0),
                alpha,
            )

    return result


def _make_wood_top(bark: Image.Image) -> Image.Image:
    bark_pixels = bark.load()
    average_red, average_green, average_blue = _average_rgb(bark)
    base_red = min(255.0, average_red + 46.0)
    base_green = min(255.0, average_green + 20.0)
    base_blue = min(255.0, max(0.0, average_blue - 5.0))

    result = Image.new("RGBA", (TILE_SIZE, TILE_SIZE))
    output = result.load()

    for y in range(TILE_SIZE):
        for x in range(TILE_SIZE):
            bark_pixel = bark_pixels[x, y]
            grain = (_luma(bark_pixel) - 128.0) / 128.0
            dx = float(x) - 7.5
            dy = float(y) - 7.5
            distance = math.sqrt(dx * dx + dy * dy)
            rings = 0.5 + 0.5 * math.sin(distance * 1.85 + grain * 2.2)
            fine = 0.5 + 0.5 * math.cos(distance * 3.2 - grain * 1.3)
            radial_shadow = max(0.0, (distance - 5.8) / 4.2)
            red = base_red * (0.82 + rings * 0.18) + fine * 6.0 - radial_shadow * 12.0 + grain * 8.0
            green = base_green * (0.84 + rings * 0.15) + fine * 4.0 - radial_shadow * 10.0 + grain * 5.0
            blue = base_blue * (0.86 + rings * 0.10) + fine * 2.0 - radial_shadow * 8.0 + grain * 2.0
            output[x, y] = (_clamp_byte(red), _clamp_byte(green), _clamp_byte(blue), 255)

    return result


def _make_planks(bark: Image.Image) -> Image.Image:
    result = _tint(
        bark,
        multiply=(1.15, 1.12, 1.08),
        add=(18.0, 12.0, 4.0),
        saturation=0.92,
    )
    output = result.load()

    for y in range(TILE_SIZE):
        band_delta = 6 if (y // 5) % 2 == 0 else -4
        seam_delta = -26 if y in (5, 10) else 0
        for x in range(TILE_SIZE):
            red, green, blue, alpha = output[x, y]
            grain_delta = ((x * 13 + y * 7) % 11) - 5
            output[x, y] = (
                _clamp_byte(red + band_delta + grain_delta * 0.5 + seam_delta),
                _clamp_byte(green + band_delta + grain_delta / 3.0 + seam_delta),
                _clamp_byte(blue + band_delta * 0.5 + seam_delta),
                alpha,
            )

    return result


def _cell_average(image: Image.Image, x0: int, y0: int, *, width: int = 4, height: int = 4) -> tuple[float, float, float]:
    pixels = image.load()
    red_sum = 0
    green_sum = 0
    blue_sum = 0
    sample_count = 0
    for y in range(y0, y0 + height):
        for x in range(x0, x0 + width):
            red, green, blue, _ = pixels[x, y]
            red_sum += red
            green_sum += green
            blue_sum += blue
            sample_count += 1
    return red_sum / sample_count, green_sum / sample_count, blue_sum / sample_count


def _make_cobblestone(stone: Image.Image) -> Image.Image:
    result = Image.new("RGBA", (TILE_SIZE, TILE_SIZE))
    output = result.load()
    source = stone.load()

    for cell_y in range(4):
        for cell_x in range(4):
            average_red, average_green, average_blue = _cell_average(stone, cell_x * 4, cell_y * 4)
            cell_shift = ((cell_x * 31 + cell_y * 17) % 19) - 9
            for y in range(cell_y * 4, cell_y * 4 + 4):
                for x in range(cell_x * 4, cell_x * 4 + 4):
                    source_red, source_green, source_blue, _ = source[x, y]
                    mortar = 18 if (x == cell_x * 4 or y == cell_y * 4) else 0
                    red = average_red * 0.55 + source_red * 0.45 + cell_shift - mortar
                    green = average_green * 0.55 + source_green * 0.45 + cell_shift - mortar
                    blue = average_blue * 0.55 + source_blue * 0.45 + cell_shift - mortar * 0.5
                    output[x, y] = (_clamp_byte(red), _clamp_byte(green), _clamp_byte(blue), 255)

    return result


def _make_gravel(stone: Image.Image, sand: Image.Image) -> Image.Image:
    result = Image.new("RGBA", (TILE_SIZE, TILE_SIZE))
    output = result.load()
    stone_pixels = stone.load()
    sand_pixels = sand.load()
    pebble_centers = (
        (3, 4, 2.6, 20.0),
        (11, 5, 2.9, 16.0),
        (6, 11, 2.4, 18.0),
        (13, 12, 2.1, 14.0),
    )

    for y in range(TILE_SIZE):
        for x in range(TILE_SIZE):
            stone_red, stone_green, stone_blue, _ = stone_pixels[x, y]
            sand_red, sand_green, sand_blue, _ = sand_pixels[x, y]
            red = stone_red * 0.72 + sand_red * 0.28 + (((x * 19 + y * 7) % 13) - 6)
            green = stone_green * 0.72 + sand_green * 0.28 + (((x * 11 + y * 5) % 9) - 4)
            blue = stone_blue * 0.78 + sand_blue * 0.22 + (((x * 5 + y * 3) % 7) - 3)

            for center_x, center_y, radius, boost in pebble_centers:
                distance = math.sqrt((x - center_x) ** 2 + (y - center_y) ** 2)
                if distance < radius:
                    influence = 1.0 - distance / radius
                    red += boost * influence
                    green += boost * 0.8 * influence
                    blue += boost * 0.7 * influence

            output[x, y] = (_clamp_byte(red), _clamp_byte(green), _clamp_byte(blue), 255)

    return result


def _make_mossy_stone(stone: Image.Image, grass: Image.Image, leaves: Image.Image) -> Image.Image:
    result = stone.copy()
    output = result.load()
    stone_pixels = stone.load()
    grass_pixels = grass.load()
    leaf_pixels = leaves.load()

    for y in range(TILE_SIZE):
        for x in range(TILE_SIZE):
            stone_red, stone_green, stone_blue, _ = stone_pixels[x, y]
            grass_red, grass_green, grass_blue, _ = grass_pixels[x, y]
            leaf_red, leaf_green, leaf_blue, _ = leaf_pixels[x, y]

            moss_mask = ((x * 37 + y * 17 + x * y * 3) % 100) / 100.0
            growth = max(0.0, (0.62 - moss_mask) * 1.6)
            growth *= 0.6 + 0.4 * (1.0 - y / 15.0)

            if growth <= 0.02:
                continue

            moss_red = grass_red * 0.45 + leaf_red * 0.55
            moss_green = grass_green * 0.45 + leaf_green * 0.55
            moss_blue = grass_blue * 0.45 + leaf_blue * 0.55
            output[x, y] = (
                _clamp_byte(stone_red * (1.0 - growth * 0.65) + moss_red * growth * 0.55),
                _clamp_byte(stone_green * (1.0 - growth * 0.50) + moss_green * growth * 0.85),
                _clamp_byte(stone_blue * (1.0 - growth * 0.70) + moss_blue * growth * 0.45),
                255,
            )

    return result


def _write_header(output_path: Path, tiles: Dict[str, Image.Image]) -> None:
    output_path.parent.mkdir(parents=True, exist_ok=True)
    with output_path.open("w", encoding="utf-8", newline="\n") as handle:
        handle.write("#pragma once\n\n")
        handle.write("#include <array>\n")
        handle.write("#include <cstdint>\n\n")
        handle.write("// This file is generated by tools/generate_block_texture_tiles.py.\n")
        handle.write("// Source images: Textures/Roche.png, Terre.png, herbe.png, neige.png,\n")
        handle.write("// sable.png, troncarbre.png, feuilles.png.\n\n")
        handle.write("namespace valcraft::generated_block_texture_tiles {\n\n")
        handle.write("using PackedTile = std::array<std::uint32_t, 256>;\n\n")

        for symbol, tile_key in OUTPUT_ORDER:
            image = tiles[tile_key]
            packed_values = []
            pixels = image.load()
            for y in range(TILE_SIZE):
                for x in range(TILE_SIZE):
                    packed_values.append(_pack_rgba(pixels[x, y]))

            handle.write(f"inline constexpr PackedTile {symbol} = {{\n")
            for row in range(TILE_SIZE):
                row_values = packed_values[row * TILE_SIZE:(row + 1) * TILE_SIZE]
                formatted = ", ".join(f"0x{value:08X}U" for value in row_values)
                suffix = "," if row != TILE_SIZE - 1 else ""
                handle.write(f"    {formatted}{suffix}\n")
            handle.write("};\n\n")

        handle.write("} // namespace valcraft::generated_block_texture_tiles\n")


def _save_preview(output_path: Path, tiles: Dict[str, Image.Image]) -> None:
    contact = Image.new("RGBA", (TILE_SIZE * len(OUTPUT_ORDER), TILE_SIZE), (0, 0, 0, 255))
    for index, (_, tile_key) in enumerate(OUTPUT_ORDER):
        contact.paste(tiles[tile_key], (index * TILE_SIZE, 0))
    output_path.parent.mkdir(parents=True, exist_ok=True)
    contact.resize((contact.width * 8, contact.height * 8), Image.Resampling.NEAREST).save(output_path)


def _generate_tiles(texture_dir: Path) -> Dict[str, Image.Image]:
    primary = {
        "stone": _load_primary_tile(texture_dir, PRIMARY_TEXTURES["stone"], contrast=1.06, brightness=1.04),
        "dirt": _load_primary_tile(texture_dir, PRIMARY_TEXTURES["dirt"], contrast=1.08, brightness=1.02),
        "grass": _load_primary_tile(texture_dir, PRIMARY_TEXTURES["grass"], contrast=1.05, saturation=1.08),
        "snow": _load_primary_tile(texture_dir, PRIMARY_TEXTURES["snow"], contrast=1.03, brightness=1.02),
        "sand": _load_primary_tile(texture_dir, PRIMARY_TEXTURES["sand"], contrast=1.04, brightness=1.02),
        "bark": _load_primary_tile(texture_dir, PRIMARY_TEXTURES["bark"], contrast=1.08, brightness=1.02),
        "leaves": _load_primary_tile(texture_dir, PRIMARY_TEXTURES["leaves"], contrast=1.08, saturation=1.08),
    }

    wood_top = _make_wood_top(primary["bark"])
    return {
        "grass_top": primary["grass"],
        "grass_side": _composite_top(primary["dirt"], primary["grass"], top_rows=5, blend_rows=1),
        "dirt": primary["dirt"],
        "stone": primary["stone"],
        "sand": primary["sand"],
        "cobblestone": _make_cobblestone(primary["stone"]),
        "gravel": _make_gravel(primary["stone"], primary["sand"]),
        "mossy_stone": _make_mossy_stone(primary["stone"], primary["grass"], primary["leaves"]),
        "wood_side": primary["bark"],
        "wood_top": wood_top,
        "planks": _make_planks(primary["bark"]),
        "leaves": primary["leaves"],
        "pine_wood_side": _tint(
            primary["bark"],
            multiply=(0.86, 0.90, 0.82),
            add=(-4.0, 0.0, 2.0),
            saturation=0.88,
        ),
        "pine_wood_top": _tint(
            wood_top,
            multiply=(0.86, 0.90, 0.84),
            add=(-6.0, 0.0, 2.0),
            saturation=0.90,
        ),
        "pine_leaves": _tint(
            primary["leaves"],
            multiply=(0.72, 0.82, 0.72),
            add=(-4.0, 0.0, 2.0),
            saturation=1.00,
        ),
        "snow_top": primary["snow"],
        "snow_side": _composite_top(primary["dirt"], primary["snow"], top_rows=6, blend_rows=1),
    }


def parse_args(argv: Iterable[str]) -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Bake the source PNG textures from Textures/ into compact 16x16 atlas tiles."
    )
    parser.add_argument(
        "--textures",
        type=Path,
        default=DEFAULT_TEXTURE_DIR,
        help=f"Source texture directory (default: {DEFAULT_TEXTURE_DIR})",
    )
    parser.add_argument(
        "--output",
        type=Path,
        default=DEFAULT_OUTPUT,
        help=f"Generated header path (default: {DEFAULT_OUTPUT})",
    )
    parser.add_argument(
        "--preview",
        type=Path,
        default=None,
        help="Optional preview output path for an upscaled contact sheet.",
    )
    return parser.parse_args(list(argv))


def main(argv: Iterable[str] | None = None) -> int:
    args = parse_args(sys.argv[1:] if argv is None else argv)
    tiles = _generate_tiles(args.textures)
    _write_header(args.output, tiles)
    if args.preview is not None:
        _save_preview(args.preview, tiles)
    return 0


if __name__ == "__main__":  # pragma: no cover - utility script
    raise SystemExit(main())
