#!/usr/bin/env python3
"""Inspect dimensions, alpha bounds, corners, and dominant colors of an image."""

from __future__ import annotations

import argparse
import json
import sys
from pathlib import Path

try:
    from PIL import Image
except ModuleNotFoundError as error:
    raise SystemExit(
        "missing Pillow; install it into a project virtualenv "
        "(not system Python), then rerun this script"
    ) from error

LANCZOS = getattr(Image, "Resampling", Image).LANCZOS


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("input", type=Path)
    parser.add_argument(
        "--alpha-threshold",
        type=int,
        default=1,
        choices=range(0, 256),
        metavar="0..255",
    )
    parser.add_argument("--json", action="store_true", help="emit machine-readable JSON")
    return parser.parse_args()


def rgba_hex(pixel: tuple[int, int, int, int]) -> str:
    return "#" + "".join(f"{channel:02X}" for channel in pixel)


def inspect(path: Path, alpha_threshold: int) -> dict[str, object]:
    with Image.open(path) as source:
        image = source.convert("RGBA")
        width, height = image.size
        alpha = image.getchannel("A")
        thresholded = alpha.point(
            lambda value: 255 if value > alpha_threshold else 0
        )
        bbox = thresholded.getbbox()
        extrema = alpha.getextrema()
        histogram = alpha.histogram()
        total = width * height
        transparent = sum(histogram[: alpha_threshold + 1])
        opaque = histogram[255]

        coordinates = {
            "top_left": (0, 0),
            "top_right": (width - 1, 0),
            "bottom_left": (0, height - 1),
            "bottom_right": (width - 1, height - 1),
        }
        corners = {
            name: rgba_hex(image.getpixel(coordinate))
            for name, coordinate in coordinates.items()
        }

        sample = image.copy()
        sample.thumbnail((64, 64), LANCZOS)
        colors = sample.getcolors(sample.width * sample.height) or []
        colors.sort(key=lambda entry: entry[0], reverse=True)
        dominant = [
            {"color": rgba_hex(pixel), "count": count}
            for count, pixel in colors[:8]
        ]

        return {
            "path": str(path.resolve()),
            "format": source.format,
            "mode": source.mode,
            "width": width,
            "height": height,
            "alpha_extrema": list(extrema),
            "alpha_bbox": list(bbox) if bbox else None,
            "transparent_percent": round(transparent * 100 / total, 2),
            "opaque_percent": round(opaque * 100 / total, 2),
            "corners": corners,
            "dominant_sample_colors": dominant,
        }


def main() -> int:
    args = parse_args()
    try:
        result = inspect(args.input, args.alpha_threshold)
    except OSError as error:
        print(f"inspect_image.py: {error}", file=sys.stderr)
        return 1

    if args.json:
        print(json.dumps(result, ensure_ascii=False, indent=2))
    else:
        print(
            f"{result['path']}: {result['width']}x{result['height']} "
            f"{result['mode']} ({result['format']})"
        )
        print(
            f"alpha bbox={result['alpha_bbox']} "
            f"transparent={result['transparent_percent']}% "
            f"opaque={result['opaque_percent']}%"
        )
        print(f"corners={result['corners']}")
        print(f"dominant={result['dominant_sample_colors']}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
