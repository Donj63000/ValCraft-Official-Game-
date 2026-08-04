#!/usr/bin/env python3
"""Je genere l'atlas SDF multi-canal deterministe de l'interface ValCraft."""

from __future__ import annotations

import argparse
import hashlib
import importlib.metadata
import math
import pathlib
import struct
import sys
from dataclasses import dataclass

import numpy as np
from PIL import Image, ImageDraw, ImageFont


MAGIC = b"VCMSDFA1"
VERSION = 1
ENCODING_MULTI_CHANNEL_SDF_RGB8 = 1
HEADER_FORMAT = "<8s16I4Q"
GLYPH_FORMAT = "<I4H5i"
KERNING_FORMAT = "<IIi"
HEADER_SIZE = struct.calcsize(HEADER_FORMAT)
GLYPH_SIZE = struct.calcsize(GLYPH_FORMAT)
KERNING_SIZE = struct.calcsize(KERNING_FORMAT)
ATLAS_SIZE = 1024
CELL_SIZE = 64
FONT_SIZE = 44
PADDING = 9
SDF_RANGE = 8
CHANNELS = 3
FNV_OFFSET = 14695981039346656037
FNV_PRIME = 1099511628211

ROOT = pathlib.Path(__file__).resolve().parents[1]
DEFAULT_FONT = ROOT / "assets" / "fonts" / "Montserrat-wght.ttf"
DEFAULT_OUTPUT = ROOT / "assets" / "fonts" / "valcraft_ui_font.msdfa"
VISUAL_REQUIREMENTS = ROOT / "tools" / "requirements-visual.txt"

# Je couvre l'ASCII imprimable, la typographie francaise courante et les
# ponctuations utilisees par les menus modernes.
EXTRA_CHARACTERS = (
    "ÀÂÄÆÇÈÉÊËÎÏÔÖŒÙÛÜŸ"
    "àâäæçèéêëîïôöœùûüÿ"
    "«»€’‘“”–—…"
)
CODEPOINTS = sorted(set(range(32, 127)) | {ord(character) for character in EXTRA_CHARACTERS})

# Pillow sans libraqm n'expose pas le GPOS de la police variable. Je conserve
# alors un petit jeu optique explicite, exprime en em, pour les paires les plus
# visibles de l'interface. Une valeur GPOS native reste prioritaire.
FALLBACK_KERNING_EM: dict[tuple[str, str], float] = {}
for left in "AÀÂÄÆ":
    for right in "TVWXY":
        FALLBACK_KERNING_EM[(left, right)] = -0.055
for left in "TVWY":
    for right in "AÀÂÄÆaàâäeéèêëoôöuùûü":
        FALLBACK_KERNING_EM[(left, right)] = -0.050
for left in "FP":
    for right in "AÀÂÄÆaàâä":
        FALLBACK_KERNING_EM[(left, right)] = -0.045
for left in "L":
    for right in "TVWXY":
        FALLBACK_KERNING_EM[(left, right)] = -0.040


@dataclass(frozen=True)
class GlyphRecord:
    codepoint: int
    atlas_x: int
    atlas_y: int
    atlas_width: int
    atlas_height: int
    advance_x64: int
    plane_left_x64: int
    plane_top_x64: int
    plane_right_x64: int
    plane_bottom_x64: int


def fnv1a64(data: bytes) -> int:
    value = FNV_OFFSET
    for byte in data:
        value ^= byte
        value = (value * FNV_PRIME) & 0xFFFFFFFFFFFFFFFF
    return value


def quantize_x64(value: float) -> int:
    # Je n'utilise pas round(), dont la regle pair/impair rend les recettes
    # moins lisibles : l'arrondi symetrique est explicite.
    scaled = value * 64.0
    return int(math.floor(scaled + 0.5) if scaled >= 0.0 else math.ceil(scaled - 0.5))


def locked_visual_dependency_versions() -> dict[str, str]:
    locked: dict[str, str] = {}
    for raw_line in VISUAL_REQUIREMENTS.read_text(encoding="utf-8").splitlines():
        line = raw_line.strip()
        if not line or line.startswith("#"):
            continue
        name, separator, version = line.partition("==")
        if not separator or not name.strip() or not version.strip():
            raise RuntimeError(
                f"visual dependency must use an exact == pin: {raw_line!r}"
            )
        locked[name.strip().lower()] = version.strip()
    return locked


def validate_visual_dependency_versions() -> None:
    locked = locked_visual_dependency_versions()
    mismatches: list[str] = []
    for distribution in ("numpy", "Pillow"):
        expected = locked.get(distribution.lower())
        if expected is None:
            raise RuntimeError(
                f"missing exact {distribution} pin in {VISUAL_REQUIREMENTS}"
            )
        actual = importlib.metadata.version(distribution)
        if actual != expected:
            mismatches.append(f"{distribution} {actual} (expected {expected})")
    if mismatches:
        details = ", ".join(mismatches)
        raise RuntimeError(
            "visual asset runtime differs from tools/requirements-visual.txt: "
            f"{details}. Refusing to generate environment-dependent bytes; run "
            f"'{sys.executable} -m pip install --requirement {VISUAL_REQUIREMENTS}'."
        )


def edt_1d(values: np.ndarray) -> np.ndarray:
    """Je calcule la transformee de distance quadratique exacte de Felzenszwalb."""
    length = int(values.shape[0])
    sites = np.empty(length, dtype=np.int32)
    boundaries = np.empty(length + 1, dtype=np.float64)
    result = np.empty(length, dtype=np.float64)
    site_count = 0
    sites[0] = 0
    boundaries[0] = -np.inf
    boundaries[1] = np.inf
    for coordinate in range(1, length):
        while True:
            previous = int(sites[site_count])
            separation = (
                (float(values[coordinate]) + coordinate * coordinate)
                - (float(values[previous]) + previous * previous)
            ) / (2.0 * (coordinate - previous))
            if separation > boundaries[site_count]:
                break
            site_count -= 1
        site_count += 1
        sites[site_count] = coordinate
        boundaries[site_count] = separation
        boundaries[site_count + 1] = np.inf
    site_count = 0
    for coordinate in range(length):
        while boundaries[site_count + 1] < coordinate:
            site_count += 1
        site = int(sites[site_count])
        delta = coordinate - site
        result[coordinate] = delta * delta + float(values[site])
    return result


def edt_2d(features: np.ndarray) -> np.ndarray:
    height, width = features.shape
    maximum_distance = float(width * width + height * height + 1)
    horizontal = np.empty((height, width), dtype=np.float64)
    for y in range(height):
        horizontal[y, :] = edt_1d(np.where(features[y, :], 0.0, maximum_distance))
    result = np.empty_like(horizontal)
    for x in range(width):
        result[:, x] = edt_1d(horizontal[:, x])
    return result


def shifted_channel(signed_distance: np.ndarray, offset: float) -> np.ndarray:
    """Je sous-echantillonne la distance a un decalage horizontal subpixel."""
    width = signed_distance.shape[1]
    source_x = np.arange(width, dtype=np.float64) + offset
    left = np.floor(source_x).astype(np.int32)
    fraction = source_x - left
    left = np.clip(left, 0, width - 1)
    right = np.clip(left + 1, 0, width - 1)
    return (
        signed_distance[:, left] * (1.0 - fraction[np.newaxis, :])
        + signed_distance[:, right] * fraction[np.newaxis, :]
    )


def encode_distance(distance: np.ndarray) -> np.ndarray:
    encoded = 127.5 + np.clip(distance / float(SDF_RANGE), -1.0, 1.0) * 127.5
    return np.floor(encoded + 0.5).astype(np.uint8)


def build_glyph_cell(font: ImageFont.FreeTypeFont, character: str) -> tuple[np.ndarray, tuple[int, int, int, int]]:
    bbox = font.getbbox(character, anchor="ls")
    if bbox is None:
        bbox = (0, 0, 0, 0)
    left, top, right, bottom = (int(value) for value in bbox)
    mask_image = Image.new("L", (CELL_SIZE, CELL_SIZE), 0)
    if right > left and bottom > top:
        draw = ImageDraw.Draw(mask_image)
        draw.text(
            (PADDING - left, PADDING - top),
            character,
            font=font,
            fill=255,
            # Je mesure et je dessine avec la même origine de baseline. Une
            # ancre d'ascender ici décalerait l'encre d'environ une ascender
            # complète et couperait le bas des lettres dans la cellule.
            anchor="ls",
        )
    mask = np.asarray(mask_image, dtype=np.uint8) >= 128
    distance_to_ink = np.sqrt(edt_2d(mask))
    distance_to_air = np.sqrt(edt_2d(~mask))
    signed_distance = distance_to_air - distance_to_ink
    red = encode_distance(shifted_channel(signed_distance, -1.0 / 3.0))
    green = encode_distance(signed_distance)
    blue = encode_distance(shifted_channel(signed_distance, 1.0 / 3.0))
    return np.stack((red, green, blue), axis=2), (left, top, right, bottom)


def build_mip_chain(base: np.ndarray) -> list[np.ndarray]:
    mips = [base]
    current = base
    while current.shape[0] > 1 or current.shape[1] > 1:
        height, width, _ = current.shape
        if height % 2 != 0 or width % 2 != 0:
            raise RuntimeError("atlas dimensions must stay divisible by two")
        values = current.astype(np.uint16)
        current = (
            values[0::2, 0::2]
            + values[0::2, 1::2]
            + values[1::2, 0::2]
            + values[1::2, 1::2]
            + 2
        ) // 4
        current = current.astype(np.uint8)
        mips.append(current)
    return mips


def load_font(path: pathlib.Path) -> ImageFont.FreeTypeFont:
    font = ImageFont.truetype(str(path), FONT_SIZE)
    # Montserrat est variable ; Regular est selectionne explicitement lorsque
    # FreeType expose les variantes nommees.
    try:
        font.set_variation_by_name("Regular")
    except (AttributeError, OSError, ValueError):
        pass
    return font


def generate(font_path: pathlib.Path) -> tuple[bytes, str]:
    font_bytes = font_path.read_bytes()
    font_sha256 = hashlib.sha256(font_bytes).hexdigest()
    font = load_font(font_path)
    columns = ATLAS_SIZE // CELL_SIZE
    if len(CODEPOINTS) > columns * columns:
        raise RuntimeError("glyph set exceeds atlas capacity")

    atlas = np.zeros((ATLAS_SIZE, ATLAS_SIZE, CHANNELS), dtype=np.uint8)
    glyphs: list[GlyphRecord] = []
    advances: dict[int, float] = {}
    for index, codepoint in enumerate(CODEPOINTS):
        character = chr(codepoint)
        cell, bbox = build_glyph_cell(font, character)
        atlas_x = (index % columns) * CELL_SIZE
        atlas_y = (index // columns) * CELL_SIZE
        atlas[
            atlas_y : atlas_y + CELL_SIZE,
            atlas_x : atlas_x + CELL_SIZE,
            :,
        ] = cell
        left, top, _, _ = bbox
        advance = float(font.getlength(character))
        advances[codepoint] = advance
        glyphs.append(
            GlyphRecord(
                codepoint=codepoint,
                atlas_x=atlas_x,
                atlas_y=atlas_y,
                atlas_width=CELL_SIZE,
                atlas_height=CELL_SIZE,
                advance_x64=quantize_x64(advance),
                plane_left_x64=quantize_x64(left - PADDING),
                plane_top_x64=quantize_x64(-top + PADDING),
                plane_right_x64=quantize_x64(left - PADDING + CELL_SIZE),
                plane_bottom_x64=quantize_x64(-top + PADDING - CELL_SIZE),
            )
        )

    kernings: list[tuple[int, int, int]] = []
    for left_codepoint in CODEPOINTS:
        left_character = chr(left_codepoint)
        for right_codepoint in CODEPOINTS:
            pair_length = float(font.getlength(left_character + chr(right_codepoint)))
            adjustment = quantize_x64(
                pair_length - advances[left_codepoint] - advances[right_codepoint]
            )
            if adjustment == 0:
                fallback = FALLBACK_KERNING_EM.get(
                    (left_character, chr(right_codepoint))
                )
                if fallback is not None:
                    adjustment = quantize_x64(fallback * FONT_SIZE)
            if adjustment != 0:
                kernings.append((left_codepoint, right_codepoint, adjustment))

    glyph_bytes = b"".join(
        struct.pack(
            GLYPH_FORMAT,
            glyph.codepoint,
            glyph.atlas_x,
            glyph.atlas_y,
            glyph.atlas_width,
            glyph.atlas_height,
            glyph.advance_x64,
            glyph.plane_left_x64,
            glyph.plane_top_x64,
            glyph.plane_right_x64,
            glyph.plane_bottom_x64,
        )
        for glyph in glyphs
    )
    kerning_bytes = b"".join(
        struct.pack(KERNING_FORMAT, left, right, adjustment)
        for left, right, adjustment in kernings
    )
    mips = build_mip_chain(atlas)
    pixel_bytes = b"".join(mip.tobytes(order="C") for mip in mips)
    glyph_offset = HEADER_SIZE
    kerning_offset = glyph_offset + len(glyph_bytes)
    pixel_offset = kerning_offset + len(kerning_bytes)
    payload = glyph_bytes + kerning_bytes + pixel_bytes
    checksum = fnv1a64(payload)
    ascent, descent = font.getmetrics()
    line_gap = max(0, int(round(FONT_SIZE * 1.2)) - ascent - descent)
    header = struct.pack(
        HEADER_FORMAT,
        MAGIC,
        VERSION,
        HEADER_SIZE,
        ENCODING_MULTI_CHANNEL_SDF_RGB8,
        ATLAS_SIZE,
        ATLAS_SIZE,
        CHANNELS,
        len(mips),
        len(glyphs),
        len(kernings),
        FONT_SIZE * 64,
        SDF_RANGE * 64,
        GLYPH_SIZE,
        KERNING_SIZE,
        ascent * 64,
        descent * 64,
        line_gap * 64,
        glyph_offset,
        kerning_offset,
        pixel_offset,
        checksum,
    )
    return header + payload, font_sha256


def parse_arguments() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--font", type=pathlib.Path, default=DEFAULT_FONT)
    parser.add_argument("--output", type=pathlib.Path, default=DEFAULT_OUTPUT)
    parser.add_argument(
        "--check",
        action="store_true",
        help="verifie que l'asset versionne correspond exactement a la recette",
    )
    return parser.parse_args()


def main() -> int:
    arguments = parse_arguments()
    try:
        validate_visual_dependency_versions()
    except RuntimeError as error:
        print(f"error: {error}", file=sys.stderr)
        return 2
    generated, font_sha256 = generate(arguments.font)
    if arguments.check:
        if not arguments.output.exists():
            print(f"missing atlas: {arguments.output}", file=sys.stderr)
            return 1
        current = arguments.output.read_bytes()
        if current != generated:
            print(f"atlas differs from deterministic recipe: {arguments.output}", file=sys.stderr)
            return 1
        print(
            f"atlas OK: {arguments.output} ({len(generated)} bytes, "
            f"font sha256 {font_sha256})"
        )
        return 0

    arguments.output.parent.mkdir(parents=True, exist_ok=True)
    arguments.output.write_bytes(generated)
    print(
        f"wrote {arguments.output} ({len(generated)} bytes, "
        f"{len(CODEPOINTS)} glyphs, font sha256 {font_sha256})"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
