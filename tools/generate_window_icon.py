from __future__ import annotations

from pathlib import Path

from PIL import Image, ImageDraw, ImageFilter

SIZE = 256
ROOT = Path(__file__).resolve().parents[1]
OUTPUT_PNG = ROOT / "Images" / "valcraft_icon.png"
OUTPUT_BMP = ROOT / "Images" / "valcraft_icon.bmp"

PIXEL_FONT = {
    "A": ["01110", "10001", "10001", "11111", "10001", "10001", "10001"],
    "C": ["01111", "10000", "10000", "10000", "10000", "10000", "01111"],
    "F": ["11111", "10000", "10000", "11110", "10000", "10000", "10000"],
    "L": ["10000", "10000", "10000", "10000", "10000", "10000", "11111"],
    "R": ["11110", "10001", "10001", "11110", "10100", "10010", "10001"],
    "T": ["11111", "00100", "00100", "00100", "00100", "00100", "00100"],
    "V": ["10001", "10001", "10001", "10001", "10001", "01010", "00100"],
}


def lerp(a: int, b: int, t: float) -> int:
    return round(a + (b - a) * t)


def vertical_gradient(image: Image.Image, top: tuple[int, int, int], bottom: tuple[int, int, int]) -> None:
    draw = ImageDraw.Draw(image)
    for y in range(image.height):
        t = y / max(image.height - 1, 1)
        color = tuple(lerp(top[index], bottom[index], t) for index in range(3))
        draw.line((0, y, image.width, y), fill=color)


def draw_glow(base: Image.Image, center: tuple[int, int], radius: int, color: tuple[int, int, int, int]) -> None:
    glow = Image.new("RGBA", base.size, (0, 0, 0, 0))
    glow_draw = ImageDraw.Draw(glow)
    glow_draw.ellipse(
        (center[0] - radius, center[1] - radius, center[0] + radius, center[1] + radius),
        fill=color,
    )
    glow = glow.filter(ImageFilter.GaussianBlur(radius=18))
    base.alpha_composite(glow)


def draw_voxel_block(base: Image.Image) -> None:
    draw = ImageDraw.Draw(base)

    top = [(128, 26), (208, 72), (128, 116), (48, 72)]
    left = [(48, 72), (128, 116), (128, 206), (48, 160)]
    right = [(208, 72), (128, 116), (128, 206), (208, 160)]

    draw.polygon(left, fill=(92, 60, 34, 255))
    draw.polygon(right, fill=(120, 76, 42, 255))
    draw.polygon(top, fill=(111, 186, 56, 255))

    for offset in range(0, 74, 10):
        draw.line((128 - offset, 116 + offset // 2, 128 - offset, 202 - offset // 2), fill=(73, 46, 27, 150), width=2)
        draw.line((128 + offset, 116 + offset // 2, 128 + offset, 202 - offset // 2), fill=(133, 87, 49, 150), width=2)

    for index in range(7):
        grass_y = 37 + index * 8
        draw.line((57 + index * 22, grass_y + 12, 68 + index * 22, grass_y + 6), fill=(153, 222, 82, 255), width=3)
        draw.line((62 + index * 22, grass_y + 14, 74 + index * 22, grass_y + 5), fill=(88, 150, 44, 255), width=3)

    draw.line((48, 72, 128, 26, 208, 72), fill=(216, 243, 170, 255), width=4)
    draw.line((48, 160, 128, 206, 208, 160), fill=(56, 36, 20, 180), width=3)
    draw.line((48, 72, 48, 160), fill=(64, 40, 23, 255), width=3)
    draw.line((208, 72, 208, 160), fill=(150, 96, 54, 255), width=3)


def draw_pixel_text(base: Image.Image, text: str, origin: tuple[int, int], scale: int) -> None:
    draw = ImageDraw.Draw(base)
    x, y = origin
    shadow = (77, 37, 10, 255)
    outline = (39, 25, 16, 255)
    fill = (246, 232, 194, 255)
    highlight = (255, 249, 232, 255)

    for index, char in enumerate(text):
        glyph = PIXEL_FONT[char]
        char_x = x + index * scale * 6
        for row_index, row in enumerate(glyph):
            for column_index, cell in enumerate(row):
                if cell != "1":
                    continue
                px = char_x + column_index * scale
                py = y + row_index * scale
                draw.rectangle((px + scale, py + scale, px + scale * 2 - 1, py + scale * 2 - 1), fill=shadow)
                draw.rectangle((px - 1, py - 1, px + scale - 1, py + scale - 1), fill=outline)
                draw.rectangle((px, py, px + scale - 1, py + scale - 1), fill=fill)
                draw.line((px, py, px + scale - 1, py), fill=highlight, width=1)


def build_icon() -> Image.Image:
    image = Image.new("RGBA", (SIZE, SIZE), (0, 0, 0, 0))
    vertical_gradient(image, (111, 165, 230), (28, 58, 98))
    draw_glow(image, (188, 58), 42, (255, 221, 146, 120))

    draw = ImageDraw.Draw(image)
    for y in range(176, SIZE):
        t = (y - 176) / max(SIZE - 176, 1)
        color = (lerp(22, 9, t), lerp(46, 18, t), lerp(38, 16, t), 255)
        draw.line((0, y, SIZE, y), fill=color)

    draw_voxel_block(image)

    draw.rounded_rectangle((24, 176, 232, 232), radius=18, fill=(28, 31, 36, 228), outline=(90, 101, 114, 255), width=4)
    draw.rectangle((34, 186, 222, 196), fill=(132, 96, 44, 190))
    draw_pixel_text(image, "VALCRAFT", (32, 190), scale=4)

    draw.rounded_rectangle((6, 6, 250, 250), radius=28, outline=(231, 239, 248, 180), width=3)
    return image


def main() -> None:
    OUTPUT_PNG.parent.mkdir(parents=True, exist_ok=True)
    icon = build_icon()
    icon.save(OUTPUT_PNG)
    icon.convert("RGB").save(OUTPUT_BMP)
    print(f"Generated {OUTPUT_PNG}")
    print(f"Generated {OUTPUT_BMP}")


if __name__ == "__main__":
    main()
