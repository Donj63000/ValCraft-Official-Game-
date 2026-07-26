#!/usr/bin/env python3
"""Compare deux captures BMP deterministes sans dependance externe."""

from __future__ import annotations

import argparse
import json
import math
import struct
import sys
import tempfile
from dataclasses import dataclass
from pathlib import Path
from typing import Iterable


@dataclass(frozen=True)
class Image:
    width: int
    height: int
    rgba: bytes


@dataclass(frozen=True)
class Mask:
    x: int
    y: int
    width: int
    height: int

    def contains(self, x: int, y: int) -> bool:
        return self.x <= x < self.x + self.width and self.y <= y < self.y + self.height


def read_bmp(path: Path) -> Image:
    data = path.read_bytes()
    if len(data) < 54 or data[:2] != b"BM":
        raise ValueError(f"{path}: fichier BMP invalide")

    pixel_offset = struct.unpack_from("<I", data, 10)[0]
    dib_size = struct.unpack_from("<I", data, 14)[0]
    if dib_size < 40:
        raise ValueError(f"{path}: en-tete DIB non pris en charge")

    width, signed_height = struct.unpack_from("<ii", data, 18)
    planes, bits_per_pixel = struct.unpack_from("<HH", data, 26)
    compression = struct.unpack_from("<I", data, 30)[0]
    if width <= 0 or signed_height == 0 or planes != 1:
        raise ValueError(f"{path}: dimensions BMP invalides")
    if bits_per_pixel not in (24, 32) or compression != 0:
        raise ValueError(f"{path}: seuls les BMP BGR/BGRA 24/32 bits non compresses sont acceptes")

    height = abs(signed_height)
    bytes_per_pixel = bits_per_pixel // 8
    row_stride = ((width * bytes_per_pixel + 3) // 4) * 4
    expected_size = pixel_offset + row_stride * height
    if expected_size > len(data):
        raise ValueError(f"{path}: donnees pixel tronquees")

    top_down = signed_height < 0
    rgba = bytearray(width * height * 4)
    for output_y in range(height):
        source_y = output_y if top_down else height - 1 - output_y
        source_row = pixel_offset + source_y * row_stride
        for x in range(width):
            source = source_row + x * bytes_per_pixel
            target = (output_y * width + x) * 4
            blue, green, red = data[source : source + 3]
            alpha = data[source + 3] if bytes_per_pixel == 4 else 255
            rgba[target : target + 4] = bytes((red, green, blue, alpha))
    return Image(width, height, bytes(rgba))


def load_masks(path: Path | None) -> list[Mask]:
    if path is None:
        return []
    payload = json.loads(path.read_text(encoding="utf-8"))
    rectangles = payload.get("rectangles", payload)
    if not isinstance(rectangles, list):
        raise ValueError("Le masque doit contenir une liste 'rectangles'")

    masks: list[Mask] = []
    for item in rectangles:
        if not isinstance(item, dict):
            raise ValueError("Chaque masque doit etre un objet JSON")
        mask = Mask(
            int(item["x"]),
            int(item["y"]),
            int(item["width"]),
            int(item["height"]),
        )
        if mask.width <= 0 or mask.height <= 0:
            raise ValueError("Les dimensions d'un masque doivent etre strictement positives")
        masks.append(mask)
    return masks


def unmasked_pixels(reference: Image, candidate: Image, masks: Iterable[Mask]):
    if (reference.width, reference.height) != (candidate.width, candidate.height):
        raise ValueError(
            "Les captures n'ont pas les memes dimensions: "
            f"{reference.width}x{reference.height} contre {candidate.width}x{candidate.height}"
        )

    mask_list = list(masks)
    for y in range(reference.height):
        for x in range(reference.width):
            if any(mask.contains(x, y) for mask in mask_list):
                continue
            offset = (y * reference.width + x) * 4
            yield (
                reference.rgba[offset : offset + 4],
                candidate.rgba[offset : offset + 4],
            )


def luminance(pixel: bytes) -> float:
    # Je calcule la luminance dans l'espace sRGB capture; la comparaison reste
    # volontairement identique sur toutes les machines et tous les pilotes.
    return 0.2126 * pixel[0] + 0.7152 * pixel[1] + 0.0722 * pixel[2]


def compare(
    reference: Image,
    candidate: Image,
    masks: Iterable[Mask],
    strong_difference: int,
) -> dict[str, float | int]:
    count = 0
    strong_count = 0
    absolute_sum = 0.0
    ref_sum = 0.0
    candidate_sum = 0.0
    ref_square_sum = 0.0
    candidate_square_sum = 0.0
    cross_sum = 0.0

    for reference_pixel, candidate_pixel in unmasked_pixels(reference, candidate, masks):
        reference_luminance = luminance(reference_pixel)
        candidate_luminance = luminance(candidate_pixel)
        ref_sum += reference_luminance
        candidate_sum += candidate_luminance
        ref_square_sum += reference_luminance * reference_luminance
        candidate_square_sum += candidate_luminance * candidate_luminance
        cross_sum += reference_luminance * candidate_luminance

        channel_difference = max(
            abs(int(reference_pixel[channel]) - int(candidate_pixel[channel]))
            for channel in range(3)
        )
        strong_count += int(channel_difference >= strong_difference)
        absolute_sum += sum(
            abs(int(reference_pixel[channel]) - int(candidate_pixel[channel]))
            for channel in range(3)
        ) / 3.0
        count += 1

    if count == 0:
        raise ValueError("Le masque exclut tous les pixels")

    reference_mean = ref_sum / count
    candidate_mean = candidate_sum / count
    reference_variance = max(ref_square_sum / count - reference_mean * reference_mean, 0.0)
    candidate_variance = max(
        candidate_square_sum / count - candidate_mean * candidate_mean,
        0.0,
    )
    covariance = cross_sum / count - reference_mean * candidate_mean
    c1 = (0.01 * 255.0) ** 2
    c2 = (0.03 * 255.0) ** 2
    denominator = (
        (reference_mean * reference_mean + candidate_mean * candidate_mean + c1)
        * (reference_variance + candidate_variance + c2)
    )
    ssim = (
        ((2.0 * reference_mean * candidate_mean + c1) * (2.0 * covariance + c2))
        / denominator
        if denominator > 0.0
        else 1.0
    )

    return {
        "compared_pixels": count,
        "ssim": max(-1.0, min(1.0, ssim)),
        "strong_difference_pixels": strong_count,
        "strong_difference_ratio": strong_count / count,
        "mean_absolute_error": absolute_sum / count,
    }


def write_test_bmp(path: Path, width: int, height: int, pixels: bytes) -> None:
    row_stride = ((width * 3 + 3) // 4) * 4
    payload_size = row_stride * height
    header = bytearray(54)
    header[:2] = b"BM"
    struct.pack_into("<I", header, 2, len(header) + payload_size)
    struct.pack_into("<I", header, 10, len(header))
    struct.pack_into("<I", header, 14, 40)
    struct.pack_into("<ii", header, 18, width, height)
    struct.pack_into("<HH", header, 26, 1, 24)
    struct.pack_into("<I", header, 34, payload_size)

    output = bytearray(header)
    for y in range(height - 1, -1, -1):
        row = bytearray()
        for x in range(width):
            offset = (y * width + x) * 4
            red, green, blue = pixels[offset : offset + 3]
            row.extend((blue, green, red))
        row.extend(b"\0" * (row_stride - len(row)))
        output.extend(row)
    path.write_bytes(output)


def run_self_test() -> int:
    with tempfile.TemporaryDirectory(prefix="valcraft-visual-compare-") as directory:
        root = Path(directory)
        original = bytes((40, 80, 120, 255) * 16)
        changed = bytearray(original)
        changed[0:4] = bytes((255, 0, 0, 255))
        original_path = root / "original.bmp"
        changed_path = root / "changed.bmp"
        write_test_bmp(original_path, 4, 4, original)
        write_test_bmp(changed_path, 4, 4, bytes(changed))

        loaded = read_bmp(original_path)
        if loaded.rgba != original:
            raise AssertionError("La lecture BMP ne restitue pas les pixels originaux")
        identical = compare(loaded, loaded, [], 32)
        if not math.isclose(float(identical["ssim"]), 1.0) or identical["strong_difference_pixels"] != 0:
            raise AssertionError("Deux captures identiques doivent obtenir un score parfait")
        different = compare(loaded, read_bmp(changed_path), [], 32)
        if different["strong_difference_pixels"] != 1:
            raise AssertionError("La difference forte volontaire n'a pas ete detectee")
        masked = compare(loaded, read_bmp(changed_path), [Mask(0, 0, 1, 1)], 32)
        if not math.isclose(float(masked["ssim"]), 1.0):
            raise AssertionError("Le masque n'exclut pas correctement la zone animee")
    return 0


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--reference", type=Path)
    parser.add_argument("--candidate", type=Path)
    parser.add_argument("--mask", type=Path)
    parser.add_argument("--output-json", type=Path)
    parser.add_argument("--minimum-ssim", type=float, default=0.985)
    parser.add_argument("--maximum-strong-ratio", type=float, default=0.01)
    parser.add_argument("--strong-difference", type=int, default=32)
    parser.add_argument("--self-test", action="store_true")
    arguments = parser.parse_args()

    if arguments.self_test:
        return run_self_test()
    if arguments.reference is None or arguments.candidate is None:
        parser.error("--reference et --candidate sont obligatoires")
    if not 0.0 <= arguments.minimum_ssim <= 1.0:
        parser.error("--minimum-ssim doit etre compris entre 0 et 1")
    if not 0.0 <= arguments.maximum_strong_ratio <= 1.0:
        parser.error("--maximum-strong-ratio doit etre compris entre 0 et 1")
    if not 1 <= arguments.strong_difference <= 255:
        parser.error("--strong-difference doit etre compris entre 1 et 255")

    result = compare(
        read_bmp(arguments.reference),
        read_bmp(arguments.candidate),
        load_masks(arguments.mask),
        arguments.strong_difference,
    )
    result["minimum_ssim"] = arguments.minimum_ssim
    result["maximum_strong_ratio"] = arguments.maximum_strong_ratio
    result["passed"] = (
        float(result["ssim"]) >= arguments.minimum_ssim
        and float(result["strong_difference_ratio"]) <= arguments.maximum_strong_ratio
    )
    serialized = json.dumps(result, indent=2, sort_keys=True) + "\n"
    if arguments.output_json is not None:
        arguments.output_json.parent.mkdir(parents=True, exist_ok=True)
        arguments.output_json.write_text(serialized, encoding="utf-8")
    sys.stdout.write(serialized)
    return 0 if bool(result["passed"]) else 1


if __name__ == "__main__":
    raise SystemExit(main())
