#!/usr/bin/env python3
"""Remove a flat edge-connected background and write an RGBA PNG."""

from __future__ import annotations

import argparse
import statistics
import sys
from collections import deque
from pathlib import Path

try:
    from PIL import Image, ImageChops, ImageColor, ImageFilter
except ModuleNotFoundError as error:
    raise SystemExit(
        "missing Pillow; install it into a project virtualenv "
        "(not system Python), then rerun this script"
    ) from error


def positive_int(value: str) -> int:
    parsed = int(value)
    if parsed < 0:
        raise argparse.ArgumentTypeError("must not be negative")
    return parsed


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("input", type=Path)
    parser.add_argument("output", type=Path)
    parser.add_argument(
        "--background",
        help="background color such as '#FFFFFF'; default samples image corners",
    )
    parser.add_argument(
        "--tolerance",
        type=positive_int,
        default=24,
        help="RGB distance from background (default: 24)",
    )
    parser.add_argument(
        "--feather",
        type=float,
        default=1.0,
        help="edge feather radius in pixels (default: 1)",
    )
    parser.add_argument(
        "--all-matching",
        action="store_true",
        help="remove matching pixels everywhere, not only regions touching an edge",
    )
    parser.add_argument("--crop", action="store_true", help="crop to resulting alpha bounds")
    parser.add_argument(
        "--padding",
        type=positive_int,
        default=0,
        help="transparent padding retained around --crop bounds",
    )
    return parser.parse_args()


def parse_rgb(value: str) -> tuple[int, int, int]:
    color = ImageColor.getrgb(value)
    return color[:3]


def sampled_background(image: Image.Image) -> tuple[int, int, int]:
    width, height = image.size
    coordinates = (
        (0, 0),
        (width - 1, 0),
        (0, height - 1),
        (width - 1, height - 1),
    )
    samples = [image.getpixel(coordinate)[:3] for coordinate in coordinates]
    return tuple(
        int(statistics.median(sample[channel] for sample in samples))
        for channel in range(3)
    )


def expand_bbox(
    bbox: tuple[int, int, int, int], padding: int, size: tuple[int, int]
) -> tuple[int, int, int, int]:
    left, top, right, bottom = bbox
    width, height = size
    return (
        max(0, left - padding),
        max(0, top - padding),
        min(width, right + padding),
        min(height, bottom + padding),
    )


def remove_background(
    args: argparse.Namespace,
) -> tuple[Image.Image, int, int, tuple[int, int, int]]:
    with Image.open(args.input) as source:
        image = source.convert("RGBA")

    width, height = image.size
    pixels = image.load()
    background = (
        parse_rgb(args.background)
        if args.background
        else sampled_background(image)
    )
    tolerance_squared = args.tolerance * args.tolerance

    candidates = bytearray(width * height)
    for y in range(height):
        for x in range(width):
            red, green, blue, alpha = pixels[x, y]
            distance = (
                (red - background[0]) ** 2
                + (green - background[1]) ** 2
                + (blue - background[2]) ** 2
            )
            if alpha > 0 and distance <= tolerance_squared:
                candidates[y * width + x] = 1

    removed = bytearray(width * height)
    if args.all_matching:
        removed[:] = candidates
    else:
        queue: deque[int] = deque()
        for x in range(width):
            queue.append(x)
            queue.append((height - 1) * width + x)
        for y in range(1, height - 1):
            queue.append(y * width)
            queue.append(y * width + width - 1)

        while queue:
            index = queue.popleft()
            if removed[index] or not candidates[index]:
                continue
            removed[index] = 1
            x = index % width
            y = index // width
            if x:
                queue.append(index - 1)
            if x + 1 < width:
                queue.append(index + 1)
            if y:
                queue.append(index - width)
            if y + 1 < height:
                queue.append(index + width)

    removed_count = sum(removed)
    if removed_count == 0:
        raise ValueError(
            "no background pixels removed; specify --background or raise --tolerance"
        )
    if removed_count >= width * height * 0.98:
        raise ValueError(
            "refusing to remove at least 98% of the image; lower --tolerance "
            "or specify --background"
        )

    cutout_mask = Image.frombytes(
        "L", image.size, bytes(0 if value else 255 for value in removed)
    )
    if args.feather < 0:
        raise ValueError("--feather must not be negative")
    if args.feather:
        cutout_mask = cutout_mask.filter(ImageFilter.GaussianBlur(args.feather))
    alpha = ImageChops.multiply(image.getchannel("A"), cutout_mask)
    image.putalpha(alpha)

    if args.crop:
        bbox = alpha.getbbox()
        if bbox is None:
            raise ValueError("result is fully transparent")
        image = image.crop(expand_bbox(bbox, args.padding, image.size))
    return image, removed_count, width * height, background


def main() -> int:
    args = parse_args()
    if args.output.suffix.lower() != ".png":
        print("make_transparent.py: output must end in .png", file=sys.stderr)
        return 1
    try:
        image, removed, source_total, background = remove_background(args)
        args.output.parent.mkdir(parents=True, exist_ok=True)
        image.save(args.output, "PNG")
    except (OSError, ValueError) as error:
        print(f"make_transparent.py: {error}", file=sys.stderr)
        return 1

    print(
        f"wrote {args.output.resolve()} ({image.width}x{image.height}, RGBA); "
        f"background=#{background[0]:02X}{background[1]:02X}{background[2]:02X}, "
        f"removed={removed * 100 / source_total:.2f}%"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
