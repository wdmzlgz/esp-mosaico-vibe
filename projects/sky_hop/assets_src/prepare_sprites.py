#!/usr/bin/env python3
"""Trim generated sprite cells into a deterministic transparent 4x2 atlas."""
from pathlib import Path
from PIL import Image

ROOT = Path(__file__).resolve().parent
source = Image.open(ROOT / "sky_hop_sprite_source.png").convert("RGBA")
pixels = source.load()
for y in range(source.height):
    for x in range(source.width):
        r, g, b, _ = pixels[x, y]
        # The generated checkerboard is connected to the outer background;
        # these neutral light pixels are not used by sprite outlines.
        if min(r, g, b) >= 205 and max(r, g, b) - min(r, g, b) <= 15:
            pixels[x, y] = (r, g, b, 0)

atlas = Image.new("RGBA", (256, 128), (0, 0, 0, 0))
cell_w, cell_h = source.width // 4, source.height // 2
for index in range(8):
    col, row = index % 4, index // 4
    cell = source.crop((col * cell_w, row * cell_h,
                        (col + 1) * cell_w, (row + 1) * cell_h))
    bounds = cell.getchannel("A").getbbox()
    if not bounds:
        raise SystemExit(f"empty sprite cell {index}")
    cell = cell.crop(bounds)
    resampling = getattr(Image, "Resampling", Image)
    cell.thumbnail((60, 60), resampling.LANCZOS)
    x = col * 64 + (64 - cell.width) // 2
    y = row * 64 + (64 - cell.height) // 2
    atlas.alpha_composite(cell, (x, y))
atlas.save(ROOT / "sky_hop_atlas.png")
