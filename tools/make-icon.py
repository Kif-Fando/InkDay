#!/usr/bin/env python3
"""Draw the InkDay icon: a calendar sheet with the sun and a list of events.

Grayscale only, so it looks the same on e-ink. Drawn at 8x and downscaled for
smooth edges. Requires Pillow (pip install pillow).

Usage: tools/make-icon.py

Writes the website/Google logo to docs/images (icon-120.png, icon-512.png) and
the reader's menu icons to icons/ (InkDay.bmp and the pressed variant
InkDay_f.bmp: 8-bit grayscale BMP, line art like the firmware's own icons).
"""
import math
import os

from PIL import Image, ImageDraw

SCALE = 8
BASE = 120
BLACK = 0
WHITE = 255
GRAY = 150
MENU_ICON_SIZE = 100


def draw_icon(size_px):
    s = BASE * SCALE
    u = SCALE  # one unit = one pixel of the 120 px icon
    image = Image.new("L", (s, s), WHITE)
    d = ImageDraw.Draw(image)

    # Background tile.
    d.rounded_rectangle((0, 0, s - 1, s - 1), radius=26 * u, fill=BLACK)

    # Calendar sheet.
    d.rounded_rectangle((18 * u, 24 * u, 102 * u, 104 * u), radius=9 * u, fill=WHITE)

    # Binder rings crossing the top edge of the sheet.
    for cx in (40 * u, 80 * u):
        d.rounded_rectangle((cx - 4 * u, 15 * u, cx + 4 * u, 33 * u), radius=4 * u,
                            fill=WHITE, outline=BLACK, width=3 * u)

    # Sun (weather).
    sun_x, sun_y, r = 40 * u, 51 * u, 8 * u
    for i in range(8):
        a = i * math.pi / 4
        d.line((sun_x + math.cos(a) * (r + 4 * u), sun_y + math.sin(a) * (r + 4 * u),
                sun_x + math.cos(a) * (r + 9 * u), sun_y + math.sin(a) * (r + 9 * u)),
               fill=BLACK, width=4 * u)
    d.ellipse((sun_x - r, sun_y - r, sun_x + r, sun_y + r), fill=BLACK)

    # Temperature lines next to the sun.
    d.rounded_rectangle((64 * u, 44 * u, 92 * u, 51 * u), radius=3 * u, fill=BLACK)
    d.rounded_rectangle((64 * u, 55 * u, 84 * u, 60 * u), radius=2 * u, fill=GRAY)

    # Event rows; the first one is "now", drawn inverted like on the dashboard.
    d.rounded_rectangle((26 * u, 71 * u, 94 * u, 83 * u), radius=3 * u, fill=BLACK)
    d.rounded_rectangle((32 * u, 75 * u, 72 * u, 79 * u), radius=2 * u, fill=WHITE)
    d.ellipse((27 * u, 88 * u, 34 * u, 95 * u), fill=BLACK)
    d.rounded_rectangle((39 * u, 88 * u, 86 * u, 95 * u), radius=3 * u, fill=GRAY)

    return image.resize((size_px, size_px), Image.LANCZOS)


def draw_menu_icon(size_px, pressed=False):
    """Line-art variant matching the firmware's outlined menu icons.

    Black strokes on white; the firmware inverts it in dark mode. The pressed
    variant is drawn white on a black rounded square.
    """
    s = MENU_ICON_SIZE * SCALE
    u = SCALE  # one unit = one pixel of the 100 px menu icon
    ink, paper = (WHITE, BLACK) if pressed else (BLACK, WHITE)
    image = Image.new("L", (s, s), WHITE)
    d = ImageDraw.Draw(image)
    if pressed:
        d.rounded_rectangle((4 * u, 4 * u, 96 * u, 96 * u), radius=14 * u, fill=BLACK)
    w = 3 * u

    # Calendar sheet outline and binder rings.
    d.rounded_rectangle((18 * u, 22 * u, 82 * u, 84 * u), radius=7 * u, outline=ink, width=w)
    for cx in (36 * u, 64 * u):
        d.rounded_rectangle((cx - 3 * u, 15 * u, cx + 3 * u, 29 * u), radius=3 * u,
                            fill=paper, outline=ink, width=w)

    # Sun outline with rays.
    sun_x, sun_y, r = 36 * u, 44 * u, 6 * u
    for i in range(8):
        a = i * math.pi / 4
        d.line((sun_x + math.cos(a) * (r + 3 * u), sun_y + math.sin(a) * (r + 3 * u),
                sun_x + math.cos(a) * (r + 7 * u), sun_y + math.sin(a) * (r + 7 * u)),
               fill=ink, width=w)
    d.ellipse((sun_x - r, sun_y - r, sun_x + r, sun_y + r), outline=ink, width=w)

    # Temperature line and two event rows.
    d.line((54 * u, 41 * u, 72 * u, 41 * u), fill=ink, width=w)
    d.line((54 * u, 48 * u, 66 * u, 48 * u), fill=ink, width=w)
    d.rounded_rectangle((26 * u, 60 * u, 74 * u, 68 * u), radius=2 * u, fill=ink)
    d.ellipse((26 * u, 72 * u, 31 * u, 77 * u), fill=ink)
    d.line((36 * u, 74.5 * u, 70 * u, 74.5 * u), fill=ink, width=w)

    return image.resize((size_px, size_px), Image.LANCZOS)


def main():
    web_dir = os.path.join("docs", "images")
    os.makedirs(web_dir, exist_ok=True)
    for size in (120, 512):
        path = os.path.join(web_dir, f"icon-{size}.png")
        draw_icon(size).save(path, optimize=True)
        print(path)

    menu_dir = "icons"
    os.makedirs(menu_dir, exist_ok=True)
    for name, image in (("InkDay.bmp", draw_menu_icon(MENU_ICON_SIZE)),
                        ("InkDay_f.bmp", draw_menu_icon(MENU_ICON_SIZE, pressed=True))):
        path = os.path.join(menu_dir, name)
        image.save(path)
        print(path)


if __name__ == "__main__":
    main()
