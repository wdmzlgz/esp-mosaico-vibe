#!/usr/bin/env python3
"""Trim/crop an image and place it on an exact-size GSP-ready PNG canvas."""

from __future__ import annotations

import argparse
import sys
from pathlib import Path

try:
    from PIL import Image, ImageChops, ImageColor, ImageDraw
except ModuleNotFoundError as error:
    raise SystemExit(
        "missing Pillow; install it into a project virtualenv "
        "(not system Python), then rerun this script"
    ) from error

LANCZOS = getattr(Image, "Resampling", Image).LANCZOS


ANCHORS = (
    "top-left",
    "top",
    "top-right",
    "left",
    "center",
    "right",
    "bottom-left",
    "bottom",
    "bottom-right",
)


def nonnegative_int(value: str) -> int:
    parsed = int(value)
    if parsed < 0:
        raise argparse.ArgumentTypeError("must not be negative")
    return parsed


def positive_int(value: str) -> int:
    parsed = int(value)
    if parsed <= 0:
        raise argparse.ArgumentTypeError("must be greater than zero")
    return parsed


def parse_crop(value: str) -> tuple[int, int, int, int]:
    try:
        x, y, width, height = (int(part.strip()) for part in value.split(","))
    except (ValueError, TypeError) as error:
        raise argparse.ArgumentTypeError(
            "use x,y,width,height, for example 12,8,128,128"
        ) from error
    if width <= 0 or height <= 0:
        raise argparse.ArgumentTypeError("crop width and height must be positive")
    return x, y, x + width, y + height


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("input", type=Path)
    parser.add_argument("output", type=Path)
    crop_group = parser.add_mutually_exclusive_group()
    crop_group.add_argument(
        "--crop",
        type=parse_crop,
        metavar="X,Y,W,H",
        help="explicit source crop",
    )
    crop_group.add_argument(
        "--trim-alpha",
        action="store_true",
        help="crop to pixels whose alpha exceeds --alpha-threshold",
    )
    parser.add_argument(
        "--alpha-threshold",
        type=nonnegative_int,
        default=1,
        help="alpha cutoff for --trim-alpha (default: 1)",
    )
    parser.add_argument(
        "--padding",
        type=nonnegative_int,
        default=0,
        help="source pixels retained around the crop (default: 0)",
    )
    parser.add_argument("--width", type=positive_int, help="exact output width")
    parser.add_argument("--height", type=positive_int, help="exact output height")
    parser.add_argument(
        "--fit",
        choices=("contain", "cover", "stretch"),
        default="contain",
        help="placement inside target size (default: contain)",
    )
    parser.add_argument(
        "--anchor",
        choices=ANCHORS,
        default="center",
        help="alignment for contain/cover (default: center)",
    )
    parser.add_argument(
        "--background",
        default="transparent",
        help="'transparent' or a Pillow color such as '#000000' (default: transparent)",
    )
    parser.add_argument(
        "--radius",
        type=nonnegative_int,
        default=0,
        help="apply a rounded alpha mask with this output-pixel radius",
    )
    return parser.parse_args()


def anchor_offset(
    container: tuple[int, int], content: tuple[int, int], anchor: str
) -> tuple[int, int]:
    available_x = container[0] - content[0]
    available_y = container[1] - content[1]
    if anchor.endswith("left") or anchor == "left":
        x = 0
    elif anchor.endswith("right") or anchor == "right":
        x = available_x
    else:
        x = available_x // 2
    if anchor.startswith("top") or anchor == "top":
        y = 0
    elif anchor.startswith("bottom") or anchor == "bottom":
        y = available_y
    else:
        y = available_y // 2
    return x, y


def expanded_crop(
    bbox: tuple[int, int, int, int],
    padding: int,
    size: tuple[int, int],
) -> tuple[int, int, int, int]:
    left, top, right, bottom = bbox
    return (
        max(0, left - padding),
        max(0, top - padding),
        min(size[0], right + padding),
        min(size[1], bottom + padding),
    )


def target_background(value: str) -> tuple[int, int, int, int]:
    if value.lower() == "transparent":
        return 0, 0, 0, 0
    color = ImageColor.getcolor(value, "RGBA")
    return color


def resize_to_target(
    image: Image.Image,
    size: tuple[int, int],
    fit: str,
    anchor: str,
    background: tuple[int, int, int, int],
) -> Image.Image:
    target_width, target_height = size
    if fit == "stretch":
        return image.resize(size, LANCZOS)

    scale_x = target_width / image.width
    scale_y = target_height / image.height
    scale = min(scale_x, scale_y) if fit == "contain" else max(scale_x, scale_y)
    resized_size = (
        max(1, round(image.width * scale)),
        max(1, round(image.height * scale)),
    )
    resized = image.resize(resized_size, LANCZOS)

    if fit == "contain":
        canvas = Image.new("RGBA", size, background)
        canvas.alpha_composite(resized, anchor_offset(size, resized_size, anchor))
        return canvas

    crop_x, crop_y = anchor_offset(resized_size, size, anchor)
    return resized.crop(
        (crop_x, crop_y, crop_x + target_width, crop_y + target_height)
    )


def process(args: argparse.Namespace) -> tuple[Image.Image, tuple[int, int]]:
    if (args.width is None) != (args.height is None):
        raise ValueError("--width and --height must be supplied together")
    if args.alpha_threshold > 255:
        raise ValueError("--alpha-threshold must be between 0 and 255")

    with Image.open(args.input) as source:
        image = source.convert("RGBA")
    original_size = image.size

    bbox = args.crop
    if args.trim_alpha:
        alpha = image.getchannel("A").point(
            lambda value: 255 if value > args.alpha_threshold else 0
        )
        bbox = alpha.getbbox()
        if bbox is None:
            raise ValueError("no visible pixels remain above --alpha-threshold")
    if bbox is not None:
        bbox = expanded_crop(bbox, args.padding, image.size)
        if bbox[0] >= bbox[2] or bbox[1] >= bbox[3]:
            raise ValueError("crop lies outside the source image")
        image = image.crop(bbox)

    if args.width is not None:
        image = resize_to_target(
            image,
            (args.width, args.height),
            args.fit,
            args.anchor,
            target_background(args.background),
        )
    if args.radius:
        radius = min(args.radius, image.width // 2, image.height // 2)
        mask = Image.new("L", image.size, 0)
        draw = ImageDraw.Draw(mask)
        draw.rectangle((radius, 0, image.width - radius, image.height), fill=255)
        draw.rectangle((0, radius, image.width, image.height - radius), fill=255)
        diameter = radius * 2
        draw.ellipse((0, 0, diameter, diameter), fill=255)
        draw.ellipse((image.width - diameter, 0, image.width, diameter), fill=255)
        draw.ellipse((0, image.height - diameter, diameter, image.height), fill=255)
        draw.ellipse(
            (
                image.width - diameter,
                image.height - diameter,
                image.width,
                image.height,
            ),
            fill=255,
        )
        alpha = ImageChops.multiply(image.getchannel("A"), mask)
        image.putalpha(alpha)
    return image, original_size


def main() -> int:
    args = parse_args()
    if args.output.suffix.lower() != ".png":
        print("crop_resize.py: output must end in .png", file=sys.stderr)
        return 1
    try:
        image, original_size = process(args)
        args.output.parent.mkdir(parents=True, exist_ok=True)
        image.save(args.output, "PNG")
    except (OSError, ValueError) as error:
        print(f"crop_resize.py: {error}", file=sys.stderr)
        return 1
    print(
        f"wrote {args.output.resolve()}: "
        f"{original_size[0]}x{original_size[1]} -> {image.width}x{image.height} RGBA"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
