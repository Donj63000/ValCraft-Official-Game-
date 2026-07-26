#!/usr/bin/env python3
"""Génère le pack déterministe de matériaux stylisés de ValCraft."""

from __future__ import annotations

import argparse
import math
import struct
import sys
from dataclasses import dataclass
from pathlib import Path
from typing import Final

MAGIC: Final = b"VCVMAT01"
FORMAT_VERSION: Final = 1
HEADER_SIZE: Final = 48
TEXTURE_COUNT: Final = 3
CHANNEL_COUNT: Final = 4
ENCODING_UNORM8: Final = 1
FLAG_COMPLETE_MIP_CHAIN: Final = 1
LAYER_RECORD_SIZE: Final = 8
PROJECT_ROOT: Final = Path(__file__).resolve().parents[1]
DEFAULT_OUTPUT: Final = PROJECT_ROOT / "assets" / "visual" / "valcraft_visual_materials.vmp"

SURFACE_ORGANIC: Final = 0
SURFACE_ARCHITECTURAL: Final = 1
SURFACE_PROXY: Final = 2
SURFACE_CUTOUT: Final = 3
SURFACE_LIQUID: Final = 4
DETAIL_PATTERNS: Final = frozenset({
    "grass",
    "earth",
    "stone",
    "sand",
    "bark",
    "gravel",
    "moss",
    "snow",
    "ore",
})


@dataclass(frozen=True)
class MaterialRecipe:
    material_id: int
    name: str
    surface_class: int
    base: tuple[int, int, int]
    accent: tuple[int, int, int]
    roughness: int
    metallic: int
    emission: int
    pattern: str
    seed: int
    normal_strength: int = 2


RECIPES: Final = (
    MaterialRecipe(1, "meadow_grass", SURFACE_ORGANIC, (66, 105, 66), (118, 143, 76), 205, 0, 0, "grass", 0x114D, 1),
    MaterialRecipe(2, "loam", SURFACE_ORGANIC, (91, 68, 54), (133, 94, 66), 224, 0, 0, "earth", 0x2207, 1),
    MaterialRecipe(3, "warm_stone", SURFACE_ORGANIC, (108, 108, 105), (151, 143, 128), 230, 0, 0, "stone", 0x3401, 2),
    MaterialRecipe(4, "sunlit_sand", SURFACE_ORGANIC, (181, 151, 103), (218, 190, 137), 215, 0, 0, "sand", 0x4505, 1),
    MaterialRecipe(5, "oak_bark", SURFACE_PROXY, (79, 55, 43), (128, 86, 57), 222, 0, 0, "bark", 0x5603, 2),
    MaterialRecipe(6, "broadleaf", SURFACE_CUTOUT, (48, 88, 59), (98, 128, 70), 200, 0, 0, "leaf", 0x6707, 1),
    MaterialRecipe(7, "torch_flame", SURFACE_PROXY, (221, 91, 30), (255, 199, 88), 82, 0, 230, "flame", 0x780B, 1),
    MaterialRecipe(8, "cobblestone", SURFACE_ARCHITECTURAL, (103, 102, 98), (147, 139, 125), 232, 0, 0, "cobble", 0x8903, 2),
    MaterialRecipe(9, "terracotta_planks", SURFACE_ARCHITECTURAL, (116, 76, 61), (166, 108, 76), 211, 0, 0, "planks", 0x9A05, 2),
    MaterialRecipe(10, "river_gravel", SURFACE_ORGANIC, (98, 99, 97), (137, 132, 119), 234, 0, 0, "gravel", 0xAB07, 2),
    MaterialRecipe(11, "mossy_stone", SURFACE_ORGANIC, (81, 91, 75), (119, 128, 82), 232, 0, 0, "moss", 0xBC0B, 2),
    MaterialRecipe(12, "powder_snow", SURFACE_ORGANIC, (198, 211, 215), (235, 239, 234), 142, 0, 0, "snow", 0xCD01, 1),
    MaterialRecipe(13, "pine_bark", SURFACE_PROXY, (67, 51, 44), (111, 78, 56), 230, 0, 0, "bark", 0xDE03, 2),
    MaterialRecipe(14, "pine_needles", SURFACE_CUTOUT, (36, 71, 56), (71, 107, 70), 216, 0, 0, "needles", 0xEF05, 1),
    MaterialRecipe(15, "tall_grass", SURFACE_CUTOUT, (62, 101, 57), (117, 137, 70), 210, 0, 0, "blades", 0x10103, 1),
    MaterialRecipe(16, "crimson_flower", SURFACE_CUTOUT, (126, 49, 65), (205, 91, 99), 190, 0, 0, "flower", 0x11201, 1),
    MaterialRecipe(17, "golden_flower", SURFACE_CUTOUT, (177, 117, 42), (231, 190, 77), 184, 0, 0, "flower", 0x12305, 1),
    MaterialRecipe(18, "dead_shrub", SURFACE_CUTOUT, (82, 65, 47), (130, 98, 65), 234, 0, 0, "twigs", 0x13407, 2),
    MaterialRecipe(19, "cactus_skin", SURFACE_PROXY, (49, 99, 67), (90, 134, 78), 207, 0, 0, "cactus", 0x1450B, 2),
    MaterialRecipe(20, "clear_water", SURFACE_LIQUID, (35, 101, 128), (72, 151, 157), 112, 0, 0, "water", 0x15601, 1),
    MaterialRecipe(21, "clear_glass", SURFACE_ARCHITECTURAL, (123, 169, 179), (195, 218, 213), 72, 0, 0, "glass", 0x16703, 1),
    MaterialRecipe(22, "bronze_armor", SURFACE_PROXY, (109, 76, 54), (174, 123, 72), 142, 200, 0, "metal", 0x17805, 1),
    MaterialRecipe(23, "wood_shield", SURFACE_PROXY, (96, 66, 48), (150, 99, 61), 214, 0, 0, "rings", 0x18907, 2),
    MaterialRecipe(24, "forged_steel", SURFACE_PROXY, (91, 101, 106), (161, 174, 173), 108, 224, 0, "metal", 0x19A0B, 1),
    MaterialRecipe(25, "leather", SURFACE_PROXY, (82, 62, 48), (136, 91, 60), 205, 0, 0, "leather", 0x1AB01, 1),
    MaterialRecipe(26, "coal_ore", SURFACE_ORGANIC, (94, 94, 90), (42, 44, 47), 238, 4, 0, "ore", 0x1BC03, 2),
    MaterialRecipe(27, "iron_ore", SURFACE_ORGANIC, (105, 104, 99), (151, 105, 80), 226, 36, 0, "ore", 0x1CD05, 2),
    MaterialRecipe(28, "gold_ore", SURFACE_ORGANIC, (107, 105, 98), (199, 157, 65), 204, 174, 0, "ore", 0x1DE07, 2),
    MaterialRecipe(29, "diamond_ore", SURFACE_ORGANIC, (100, 105, 102), (74, 173, 167), 176, 128, 6, "ore", 0x1EF0B, 2),
    MaterialRecipe(30, "alloy_ore", SURFACE_ORGANIC, (101, 98, 101), (139, 98, 157), 184, 188, 4, "ore", 0x20101, 2),
    MaterialRecipe(31, "tool_wood_steel", SURFACE_PROXY, (90, 75, 64), (151, 163, 162), 162, 142, 0, "tool", 0x21203, 2),
)


def _clamp_byte(value: int) -> int:
    return max(0, min(255, value))


def _fnv1a32(data: bytes) -> int:
    result = 2166136261
    for byte in data:
        result ^= byte
        result = (result * 16777619) & 0xFFFFFFFF
    return result


def _fnv1a64(data: bytes | bytearray) -> int:
    result = 14695981039346656037
    for byte in data:
        result ^= byte
        result = (result * 1099511628211) & 0xFFFFFFFFFFFFFFFF
    return result


def _hash8(x: int, y: int, seed: int) -> int:
    value = (seed ^ (x * 0x9E3779B1) ^ (y * 0x85EBCA77)) & 0xFFFFFFFF
    value ^= value >> 16
    value = (value * 0x7FEB352D) & 0xFFFFFFFF
    value ^= value >> 15
    value = (value * 0x846CA68B) & 0xFFFFFFFF
    value ^= value >> 16
    return value & 0xFF


def _smooth_fixed(local: int, cell_size: int) -> int:
    value = (local << 16) // cell_size
    squared = (value * value) >> 16
    return (squared * ((3 << 16) - 2 * value)) >> 16


def _lerp_fixed(left: int, right: int, blend: int) -> int:
    return left + (((right - left) * blend) >> 16)


def _value_noise(x: int, y: int, size: int, cell_size: int, seed: int) -> int:
    cell_count = max(1, size // cell_size)
    cell_x = x // cell_size
    cell_y = y // cell_size
    next_x = (cell_x + 1) % cell_count
    next_y = (cell_y + 1) % cell_count
    cell_x %= cell_count
    cell_y %= cell_count
    blend_x = _smooth_fixed(x % cell_size, cell_size)
    blend_y = _smooth_fixed(y % cell_size, cell_size)
    top = _lerp_fixed(_hash8(cell_x, cell_y, seed),
                      _hash8(next_x, cell_y, seed),
                      blend_x)
    bottom = _lerp_fixed(_hash8(cell_x, next_y, seed),
                         _hash8(next_x, next_y, seed),
                         blend_x)
    return _lerp_fixed(top, bottom, blend_y)


def _fractal_noise(x: int, y: int, size: int, seed: int) -> int:
    weighted = 0
    total_weight = 0
    for octave, weight in ((32, 8), (16, 5), (8, 3), (4, 1)):
        cell_size = max(2, min(size, octave))
        weighted += _value_noise(x, y, size, cell_size, seed + octave * 97) * weight
        total_weight += weight
    return weighted // total_weight


def _cellular_noise(x: int,
                    y: int,
                    size: int,
                    cell_size: int,
                    seed: int) -> int:
    """Je produis des amas circulaires périodiques sans axe privilégié."""
    cell_size = max(2, min(size, cell_size))
    cell_count = max(1, size // cell_size)
    source_cell_x = x // cell_size
    source_cell_y = y // cell_size
    nearest_squared = size * size * 2

    for offset_y in range(-1, 2):
        for offset_x in range(-1, 2):
            cell_x = (source_cell_x + offset_x) % cell_count
            cell_y = (source_cell_y + offset_y) % cell_count
            feature_x = (
                cell_x * cell_size +
                _hash8(cell_x, cell_y, seed ^ 0x35A9) * cell_size // 256
            ) % size
            feature_y = (
                cell_y * cell_size +
                _hash8(cell_x, cell_y, seed ^ 0xA713) * cell_size // 256
            ) % size
            delta_x = abs(x - feature_x)
            delta_y = abs(y - feature_y)
            delta_x = min(delta_x, size - delta_x)
            delta_y = min(delta_y, size - delta_y)
            nearest_squared = min(
                nearest_squared,
                delta_x * delta_x + delta_y * delta_y,
            )

    distance = math.isqrt(nearest_squared)
    return 255 - min(255, distance * 384 // cell_size)


def _organic_noise(x: int, y: int, size: int, seed: int) -> int:
    """Je casse la grille du value-noise en mélangeant plusieurs repères."""
    primary = _fractal_noise(x, y, size, seed)
    diagonal = _fractal_noise(
        (x + y) % size,
        (x - y) % size,
        size,
        seed ^ 0x6D2B,
    )
    cells = _cellular_noise(
        x,
        y,
        size,
        max(8, size // 8),
        seed ^ 0xB529,
    )
    return _clamp_byte((primary * 5 + diagonal * 3 + cells * 2) // 10)


def _isotropic_detail_band(x: int,
                           y: int,
                           size: int,
                           cell_size: int,
                           seed: int) -> int:
    """Je mélange deux repères pour obtenir un grain fin sans direction dominante."""
    axis = _value_noise(x, y, size, cell_size, seed)
    diagonal = _value_noise(
        (x + y) % size,
        (x - y) % size,
        size,
        cell_size,
        seed ^ 0x6A09E667,
    )
    return (axis + diagonal) // 2


def _triangle_wave(value: int, period: int) -> int:
    folded = value % period
    half = period // 2
    return folded if folded <= half else period - folded


def _mix_color(base: tuple[int, int, int],
               accent: tuple[int, int, int],
               blend: int) -> tuple[int, int, int]:
    return tuple(base[channel] + ((accent[channel] - base[channel]) * blend // 255)
                 for channel in range(3))


def _surface_sample(recipe: MaterialRecipe,
                    x: int,
                    y: int,
                    size: int) -> tuple[tuple[int, int, int, int], int, tuple[int, int, int, int]]:
    noise = _organic_noise(x, y, size, recipe.seed)
    fine = _hash8(x, y, recipe.seed ^ 0x51A7)
    micro = 128
    meso = 128
    if recipe.pattern in DETAIL_PATTERNS:
        # Je ne paie les deux bandes supplémentaires que pour les matériaux
        # qui les consomment; le générateur hors ligne reste rapide en CI.
        micro = _isotropic_detail_band(
            x,
            y,
            size,
            max(2, size // 64),
            recipe.seed ^ 0x243F6A88,
        )
        meso = _isotropic_detail_band(
            x,
            y,
            size,
            max(4, size // 32),
            recipe.seed ^ 0xB7E15162,
        )
    height = 128 + (noise - 128) // 4
    blend = 112 + (noise - 128) // 3
    detail_luma = 0
    alpha = 255
    roughness = recipe.roughness + (fine - 128) // 12
    metallic = recipe.metallic
    emission = recipe.emission
    occlusion = 235 + (noise - 128) // 12

    if recipe.pattern == "grass":
        clumps = _cellular_noise(x, y, size, max(8, size // 6), recipe.seed ^ 0x71D3)
        pores = _cellular_noise(x, y, size, max(4, size // 24), recipe.seed ^ 0x29B7)
        height = _clamp_byte(124 + (noise - 128) // 6 + (pores - 128) // 14)
        # Je separe le volume macro du grain proche : les grandes taches restent
        # douces, tandis qu'un detail isotrope fin evite l'aspect peinture floue.
        blend = _clamp_byte(
            104 + (noise - 128) // 8 +
            (clumps - 128) // 10 + (fine - 128) // 12
        )
        detail_luma = (micro - 128) // 22 + (meso - 128) // 38
    elif recipe.pattern == "earth":
        grains = _cellular_noise(x, y, size, max(4, size // 24), recipe.seed ^ 0x3FA1)
        pebble = max(0, grains - 212)
        height = _clamp_byte(122 + (noise - 128) // 5 + pebble // 4)
        blend = _clamp_byte(
            100 + (noise - 128) // 8 +
            pebble // 5 + (fine - 128) // 11
        )
        detail_luma = (micro - 128) // 19 + (meso - 128) // 35
    elif recipe.pattern == "stone":
        plates = _cellular_noise(x, y, size, max(8, size // 7), recipe.seed ^ 0x7731)
        height = _clamp_byte(124 + (noise - 128) // 6 + (plates - 128) // 10)
        blend = _clamp_byte(
            102 + (noise - 128) // 8 +
            (plates - 128) // 11 + (fine - 128) // 14
        )
        detail_luma = (micro - 128) // 22 + (meso - 128) // 38
    elif recipe.pattern == "sand":
        grains = _cellular_noise(x, y, size, max(4, size // 32), recipe.seed ^ 0x4D17)
        height = _clamp_byte(126 + (noise - 128) // 10 + (grains - 128) // 18)
        blend = _clamp_byte(
            108 + (noise - 128) // 9 +
            (grains - 128) // 18 + (fine - 128) // 18
        )
        detail_luma = (micro - 128) // 32 + (meso - 128) // 48
        roughness -= 8
    elif recipe.pattern == "bark":
        plates = _cellular_noise(x, y, size, max(8, size // 10), recipe.seed ^ 0x846D)
        pores = _cellular_noise(x, y, size, max(4, size // 28), recipe.seed ^ 0x19B3)
        height = _clamp_byte(
            123 + (noise - 128) // 5 +
            (plates - 128) // 10 + (pores - 128) // 18
        )
        blend = _clamp_byte(
            100 + (noise - 128) // 8 +
            (plates - 128) // 12 + (fine - 128) // 12
        )
        detail_luma = (micro - 128) // 24 + (meso - 128) // 40
    elif recipe.pattern == "leaf":
        cluster = _cellular_noise(x, y, size, max(8, size // 8), recipe.seed ^ 0x4B31)
        holes = _cellular_noise(x, y, size, max(4, size // 24), recipe.seed ^ 0x98D5)
        alpha = 255 if cluster > 34 and not (holes > 224 and fine < 96) else 0
        height = _clamp_byte(126 + (noise - 128) // 6 + (cluster - 128) // 12)
        blend = _clamp_byte(
            102 + (noise - 128) // 5 +
            (cluster - 128) // 12 + (fine - 128) // 14
        )
        occlusion -= 8
    elif recipe.pattern == "flame":
        center = abs((x * 2 + (_triangle_wave(y, 13) - 6)) - (size - 1))
        allowed = max(2, (size - y) * 3 // 5)
        alpha = 255 if center < allowed else 0
        vertical = y * 255 // max(1, size - 1)
        height = _clamp_byte(220 - vertical // 2)
        blend = _clamp_byte(255 - vertical * 3 // 4)
        emission = alpha
        occlusion = 255
    elif recipe.pattern == "cobble":
        spacing = max(8, size // 8)
        seam_x = min(x % spacing, spacing - 1 - x % spacing)
        shifted_x = (x + (spacing // 2 if (y // spacing) % 2 else 0)) % spacing
        seam_x = min(shifted_x, spacing - 1 - shifted_x)
        seam_y = min(y % spacing, spacing - 1 - y % spacing)
        seam = min(seam_x, seam_y)
        height = _clamp_byte(52 + noise // 2 + min(4, seam) * 22)
        blend = _clamp_byte(38 + noise // 2 + min(4, seam) * 14)
        occlusion -= max(0, 4 - seam) * 12
    elif recipe.pattern == "planks":
        spacing = max(8, size // 8)
        seam = min(y % spacing, spacing - 1 - y % spacing)
        grain = _triangle_wave(x * 2 + noise // 20, 23)
        height = _clamp_byte(68 + grain * 10 + min(3, seam) * 19)
        blend = _clamp_byte(62 + noise // 2 + grain * 8)
        occlusion -= max(0, 3 - seam) * 15
    elif recipe.pattern == "gravel":
        pebbles = _cellular_noise(x, y, size, max(4, size // 18), recipe.seed ^ 0xA55A)
        broad = _cellular_noise(x, y, size, max(8, size // 9), recipe.seed ^ 0x6C8F)
        height = _clamp_byte(122 + (pebbles - 128) // 5 + (noise - 128) // 10)
        blend = _clamp_byte(
            104 + (broad - 128) // 7 +
            (noise - 128) // 9 + (fine - 128) // 12
        )
        detail_luma = (micro - 128) // 19 + (meso - 128) // 30
    elif recipe.pattern == "moss":
        moss = _organic_noise(x, y, size, recipe.seed ^ 0x2F19)
        cushions = _cellular_noise(x, y, size, max(8, size // 7), recipe.seed ^ 0xC43D)
        height = _clamp_byte(124 + (noise - 128) // 8 + (cushions - 128) // 9)
        blend = _clamp_byte(
            102 + (noise - 128) // 10 +
            (moss - 128) // 5 + (fine - 128) // 14
        )
        detail_luma = (micro - 128) // 24 + (meso - 128) // 38
    elif recipe.pattern == "snow":
        pillows = _cellular_noise(x, y, size, max(8, size // 6), recipe.seed ^ 0x51E7)
        height = _clamp_byte(130 + (noise - 128) // 12 + (pillows - 128) // 14)
        blend = _clamp_byte(
            122 + (noise - 128) // 12 +
            (pillows - 128) // 15 + (fine - 128) // 18
        )
        detail_luma = (micro - 128) // 38 + (meso - 128) // 54
        occlusion = 246
        roughness -= 14
    elif recipe.pattern == "needles":
        clusters = _cellular_noise(x, y, size, max(8, size // 8), recipe.seed ^ 0xA6D1)
        needles = _cellular_noise(x, y, size, max(3, size // 32), recipe.seed ^ 0x37C9)
        alpha = 255 if clusters > 58 and needles > 88 else 0
        height = _clamp_byte(124 + (clusters - 128) // 9 + (needles - 128) // 12)
        blend = _clamp_byte(96 + (noise - 128) // 3 + (clusters - 128) // 10)
    elif recipe.pattern == "blades":
        patches = _cellular_noise(x, y, size, max(8, size // 9), recipe.seed ^ 0x9137)
        wisps = _cellular_noise(x, y, size, max(3, size // 32), recipe.seed ^ 0x5B6D)
        alpha = 255 if patches > 76 and wisps > 116 else 0
        height = _clamp_byte(123 + (patches - 128) // 10 + (wisps - 128) // 12)
        blend = _clamp_byte(98 + (noise - 128) // 3 + (patches - 128) // 12)
    elif recipe.pattern == "flower":
        center_x = size // 2 + (_hash8(y // 8, 0, recipe.seed) % 7) - 3
        local_x = x - center_x
        local_y = (y % max(16, size // 4)) - max(8, size // 8)
        radius_squared = local_x * local_x + local_y * local_y
        stem = abs(local_x) <= 1
        petal = radius_squared < max(16, size // 8) ** 2 and ((abs(local_x) + abs(local_y)) % 7) < 5
        alpha = 255 if stem or petal else 0
        height = 224 if petal else 146
        blend = 232 if petal else 62
    elif recipe.pattern == "twigs":
        twig_a = _triangle_wave(x * 2 - y, 31) < 3
        twig_b = _triangle_wave(x * 3 + y * 2, 37) < 3
        alpha = 255 if twig_a or twig_b else 0
        height = _clamp_byte(118 + noise // 3)
        blend = _clamp_byte(84 + noise // 2)
    elif recipe.pattern == "cactus":
        areoles = _cellular_noise(x, y, size, max(4, size // 18), recipe.seed ^ 0xE329)
        height = _clamp_byte(125 + (noise - 128) // 7 + (areoles - 128) // 14)
        blend = _clamp_byte(102 + (noise - 128) // 3 + (areoles - 128) // 12)
        if fine > 248:
            blend = _clamp_byte(blend + 28)
    elif recipe.pattern == "water":
        ripples = _cellular_noise(x, y, size, max(8, size // 6), recipe.seed ^ 0xC519)
        height = _clamp_byte(127 + (noise - 128) // 10 + (ripples - 128) // 18)
        blend = _clamp_byte(104 + (noise - 128) // 4 + (ripples - 128) // 12)
        alpha = 198
        roughness -= 16
        occlusion = 255
    elif recipe.pattern == "glass":
        streak = _triangle_wave(x + y, max(12, size // 3))
        height = _clamp_byte(122 + noise // 16 + (8 if streak < 2 else 0))
        blend = 220 if streak < 2 else _clamp_byte(96 + noise // 4)
        alpha = 72 if streak < 2 else 38
        occlusion = 255
        roughness -= 20
    elif recipe.pattern == "metal":
        scratch = _triangle_wave(x * 5 + y, 47)
        height = _clamp_byte(118 + noise // 8 + (18 if scratch < 2 else 0))
        blend = _clamp_byte(92 + noise // 2 + (48 if scratch < 2 else 0))
        roughness += (fine - 128) // 18
    elif recipe.pattern == "rings":
        center = size // 2
        distance = math.isqrt((x - center) ** 2 + (y - center) ** 2)
        ring = _triangle_wave(distance + noise // 35, 11)
        height = _clamp_byte(84 + ring * 18 + noise // 5)
        blend = _clamp_byte(62 + ring * 15 + noise // 3)
    elif recipe.pattern == "leather":
        pore = 36 if fine > 238 else -22 if fine < 20 else 0
        height = _clamp_byte(118 + noise // 4 + pore)
        blend = _clamp_byte(82 + noise // 2 + pore // 2)
    elif recipe.pattern == "ore":
        deposits = _cellular_noise(x, y, size, max(8, size // 10), recipe.seed ^ 0xC6EF)
        exposed = abs(noise - deposits) < 23 and fine > 72
        height = _clamp_byte(122 + (noise - 128) // 7 + (24 if exposed else 0))
        blend = 204 if exposed else _clamp_byte(72 + (noise - 128) // 5)
        metallic = recipe.metallic if exposed else recipe.metallic // 8
        emission = recipe.emission if exposed else 0
        detail_luma = (micro - 128) // 24 + (meso - 128) // 40
    elif recipe.pattern == "tool":
        diagonal = _triangle_wave(x * 2 + y, max(16, size // 3))
        steel = diagonal < max(4, size // 16)
        height = _clamp_byte(105 + noise // 5 + (28 if steel else 0))
        blend = 220 if steel else _clamp_byte(48 + noise // 3)
        metallic = recipe.metallic if steel else 0

    color = _mix_color(recipe.base, recipe.accent, _clamp_byte(blend))
    if detail_luma != 0:
        # Je conserve les grandes masses de couleur stylisées, mais j'ajoute
        # un relief proche réellement visible et stable dans les mipmaps.
        color = tuple(_clamp_byte(channel + detail_luma) for channel in color)
        height = _clamp_byte(height + detail_luma)
        roughness += detail_luma // 2
    albedo = (color[0], color[1], color[2], alpha)
    orm_emission = (
        _clamp_byte(occlusion),
        _clamp_byte(roughness),
        _clamp_byte(metallic),
        _clamp_byte(emission),
    )
    return albedo, _clamp_byte(height), orm_emission


def _build_base_textures(recipe: MaterialRecipe,
                         size: int) -> tuple[bytearray, bytearray, bytearray]:
    albedo = bytearray(size * size * CHANNEL_COUNT)
    height_values = bytearray(size * size)
    orm_emission = bytearray(size * size * CHANNEL_COUNT)

    for y in range(size):
        for x in range(size):
            color, height, orm = _surface_sample(recipe, x, y, size)
            pixel_offset = (y * size + x) * CHANNEL_COUNT
            albedo[pixel_offset:pixel_offset + CHANNEL_COUNT] = bytes(color)
            height_values[y * size + x] = height
            orm_emission[pixel_offset:pixel_offset + CHANNEL_COUNT] = bytes(orm)

    normal_height = bytearray(size * size * CHANNEL_COUNT)
    # Je garde le relief lisible sans fabriquer de normales presque tangentes :
    # le modelé doit accompagner la surface organique, pas devenir un motif noir.
    normal_scale = recipe.normal_strength
    for y in range(size):
        for x in range(size):
            left = height_values[y * size + ((x - 1) % size)]
            right = height_values[y * size + ((x + 1) % size)]
            up = height_values[((y - 1) % size) * size + x]
            down = height_values[((y + 1) % size) * size + x]
            normal_x = (int(left) - int(right)) * normal_scale
            normal_y = (int(up) - int(down)) * normal_scale
            normal_z = 512
            length = max(1, math.isqrt(normal_x * normal_x + normal_y * normal_y + normal_z * normal_z))
            pixel_offset = (y * size + x) * CHANNEL_COUNT
            normal_height[pixel_offset] = _clamp_byte(128 + normal_x * 127 // length)
            normal_height[pixel_offset + 1] = _clamp_byte(128 + normal_y * 127 // length)
            normal_height[pixel_offset + 2] = _clamp_byte(128 + normal_z * 127 // length)
            normal_height[pixel_offset + 3] = height_values[y * size + x]

    return albedo, normal_height, orm_emission


def _downsample_rgba(source: bytearray,
                     width: int,
                     height: int,
                     semantic: str,
                     preserve_alpha_coverage: bool) -> tuple[bytearray, int, int]:
    target_width = max(1, width // 2)
    target_height = max(1, height // 2)
    target = bytearray(target_width * target_height * CHANNEL_COUNT)

    for target_y in range(target_height):
        for target_x in range(target_width):
            samples: list[tuple[int, int, int, int]] = []
            for offset_y in range(2):
                for offset_x in range(2):
                    source_x = min(width - 1, target_x * 2 + offset_x)
                    source_y = min(height - 1, target_y * 2 + offset_y)
                    source_offset = (source_y * width + source_x) * CHANNEL_COUNT
                    samples.append(tuple(source[source_offset + channel]
                                         for channel in range(CHANNEL_COUNT)))

            target_offset = (target_y * target_width + target_x) * CHANNEL_COUNT
            if semantic == "normal_height":
                normal_x = sum(sample[0] * 2 - 255 for sample in samples)
                normal_y = sum(sample[1] * 2 - 255 for sample in samples)
                normal_z = sum(sample[2] * 2 - 255 for sample in samples)
                length = max(1, math.isqrt(normal_x * normal_x + normal_y * normal_y + normal_z * normal_z))
                target[target_offset] = _clamp_byte(128 + normal_x * 127 // length)
                target[target_offset + 1] = _clamp_byte(128 + normal_y * 127 // length)
                target[target_offset + 2] = _clamp_byte(128 + normal_z * 127 // length)
                target[target_offset + 3] = sum(sample[3] for sample in samples) // len(samples)
            elif semantic == "albedo":
                alpha_sum = sum(sample[3] for sample in samples)
                for channel in range(3):
                    if alpha_sum > 0:
                        linear_average = (
                            sum(
                                sample[channel] * sample[channel] * sample[3]
                                for sample in samples
                            ) + alpha_sum // 2
                        ) // alpha_sum
                    else:
                        linear_average = (
                            sum(sample[channel] * sample[channel] for sample in samples) +
                            len(samples) // 2
                        ) // len(samples)
                    # Je moyenne l'albédo dans un espace linéaire déterministe
                    # (gamma 2), car le GPU charge ensuite le tableau en sRGB.
                    target[target_offset + channel] = _clamp_byte(
                        math.isqrt(linear_average)
                    )
                target[target_offset + 3] = alpha_sum // len(samples)
            else:
                for channel in range(CHANNEL_COUNT):
                    target[target_offset + channel] = (
                        sum(sample[channel] for sample in samples) // len(samples)
                    )

    if semantic == "albedo" and preserve_alpha_coverage:
        alpha_threshold = 118
        source_pixels = width * height
        target_pixels = target_width * target_height
        source_opaque = sum(
            1
            for offset in range(3, len(source), CHANNEL_COUNT)
            if source[offset] >= alpha_threshold
        )
        desired_opaque = (
            source_opaque * target_pixels + source_pixels // 2
        ) // source_pixels
        if source_opaque > 0:
            desired_opaque = max(1, desired_opaque)
        desired_opaque = min(target_pixels, desired_opaque)

        ranked_pixels = sorted(
            range(target_pixels),
            key=lambda index: (-target[index * CHANNEL_COUNT + 3], index),
        )
        retained = set(ranked_pixels[:desired_opaque])
        for index in range(target_pixels):
            alpha_offset = index * CHANNEL_COUNT + 3
            if index in retained:
                target[alpha_offset] = max(target[alpha_offset], alpha_threshold)
            else:
                target[alpha_offset] = min(target[alpha_offset], alpha_threshold - 1)

    return target, target_width, target_height


def _mip_chain(base: bytearray,
               size: int,
               semantic: str,
               preserve_alpha_coverage: bool = False) -> bytearray:
    result = bytearray(base)
    current = base
    width = size
    height = size
    while width > 1 or height > 1:
        current, width, height = _downsample_rgba(
            current,
            width,
            height,
            semantic,
            preserve_alpha_coverage,
        )
        result.extend(current)
    return result


def _build_pack(size: int) -> bytes:
    if size < 16 or size > 256 or size & (size - 1):
        raise ValueError("La taille doit être une puissance de deux comprise entre 16 et 256.")

    for expected_id, recipe in enumerate(RECIPES, start=1):
        if recipe.material_id != expected_id:
            raise ValueError("Le catalogue des matériaux doit rester dense et ordonné.")

    layer_table = bytearray()
    payload = bytearray()
    for recipe in RECIPES:
        layer_table.extend(struct.pack(
            "<HBBI",
            recipe.material_id,
            recipe.surface_class,
            0,
            _fnv1a32(recipe.name.encode("ascii")),
        ))
        albedo, normal_height, orm_emission = _build_base_textures(recipe, size)
        payload.extend(_mip_chain(
            albedo,
            size,
            "albedo",
            recipe.surface_class == SURFACE_CUTOUT,
        ))
        payload.extend(_mip_chain(normal_height, size, "normal_height"))
        payload.extend(_mip_chain(orm_emission, size, "data"))

    mip_count = size.bit_length()
    content = layer_table + payload
    header = struct.pack(
        "<8sHHHHHHBBBBIIQQ",
        MAGIC,
        FORMAT_VERSION,
        HEADER_SIZE,
        size,
        size,
        len(RECIPES),
        mip_count,
        TEXTURE_COUNT,
        CHANNEL_COUNT,
        ENCODING_UNORM8,
        FLAG_COMPLETE_MIP_CHAIN,
        LAYER_RECORD_SIZE,
        len(layer_table),
        len(payload),
        _fnv1a64(content),
    )
    if len(header) != HEADER_SIZE:
        raise AssertionError("La taille de l'en-tête généré est invalide.")
    return header + content


def _parse_arguments() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--output",
        type=Path,
        default=DEFAULT_OUTPUT,
        help=f"Chemin du pack généré (défaut : {DEFAULT_OUTPUT})",
    )
    parser.add_argument(
        "--size",
        type=int,
        default=128,
        help="Résolution carrée des couches (puissance de deux, 16 à 256).",
    )
    parser.add_argument(
        "--check",
        action="store_true",
        help="Vérifie que le pack existant est à jour sans le réécrire.",
    )
    return parser.parse_args()


def main() -> int:
    arguments = _parse_arguments()
    try:
        generated = _build_pack(arguments.size)
    except (ValueError, OSError) as error:
        sys.stderr.write(f"Erreur de génération : {error}\n")
        return 2

    output = arguments.output.resolve()
    if arguments.check:
        try:
            existing = output.read_bytes()
        except OSError as error:
            sys.stderr.write(f"Pack absent ou illisible : {error}\n")
            return 1
        if existing != generated:
            sys.stderr.write(
                f"Le pack {output} n'est pas synchronisé avec ses recettes.\n"
            )
            return 1
        print(
            f"Pack déterministe vérifié : {output} "
            f"({len(generated)} octets, checksum 0x{_fnv1a64(generated[HEADER_SIZE:]):016X})."
        )
        return 0

    # Je remplace le fichier seulement après avoir produit le pack complet.
    output.parent.mkdir(parents=True, exist_ok=True)
    temporary_output = output.with_suffix(output.suffix + ".tmp")
    temporary_output.write_bytes(generated)
    temporary_output.replace(output)
    print(
        f"Pack généré : {output} "
        f"({len(generated)} octets, {len(RECIPES)} couches, "
        f"checksum 0x{_fnv1a64(generated[HEADER_SIZE:]):016X})."
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
