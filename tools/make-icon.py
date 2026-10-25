#!/usr/bin/env python3
"""Draw the InkDay icon: a calendar sheet with the sun and a list of events.

Grayscale only, so it looks the same on e-ink. Drawn at 8x and downscaled for
smooth edges. Requires Pillow (pip install pillow).

Usage: tools/make-icon.py [output directory]   (default: docs/images)
"""
import math
import os
import sys

from PIL import Image, ImageDraw

SCALE = 8
BASE = 120
BLACK = 0
WHITE = 255
GRAY = 150


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


def main():
    out_dir = sys.argv[1] if len(sys.argv) > 1 else os.path.join("docs", "images")
    os.makedirs(out_dir, exist_ok=True)
    for size in (120, 512):
        path = os.path.join(out_dir, f"icon-{size}.png")
        draw_icon(size).save(path, optimize=True)
        print(path)


if __name__ == "__main__":
    main()
