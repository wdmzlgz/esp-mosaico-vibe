#!/usr/bin/env python3
"""Convert an SVG file or an inline JS/HTML SVG property to a PNG."""

from __future__ import annotations

import argparse
import ast
import base64
import json
import re
import sys
from pathlib import Path
from urllib.parse import unquote_to_bytes

try:
    import cairosvg
    from PIL import Image
except ModuleNotFoundError as error:
    raise SystemExit(
        f"missing Python dependency: {error.name}; install Pillow and cairosvg "
        "into a project virtualenv (not system Python), then rerun this script"
    ) from error


def positive_int(value: str) -> int:
    parsed = int(value)
    if parsed <= 0:
        raise argparse.ArgumentTypeError("must be greater than zero")
    return parsed


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description=(
            "Convert a standalone SVG, or a quoted SVG/data-URI property from "
            "a JS/HTML file, to PNG."
        )
    )
    parser.add_argument("source", type=Path, help="SVG, JS, or HTML source file")
    parser.add_argument("output", type=Path, help="destination .png path")
    parser.add_argument(
        "--key",
        help="property name whose quoted SVG or SVG data URI should be extracted",
    )
    parser.add_argument("--width", type=positive_int, help="exact output width")
    parser.add_argument("--height", type=positive_int, help="exact output height")
    parser.add_argument(
        "--background-color",
        help="optional CairoSVG background color; transparency is kept by default",
    )
    return parser.parse_args()


def decode_literal(literal: str) -> str:
    try:
        value = json.loads(literal) if literal.startswith('"') else ast.literal_eval(literal)
    except (ValueError, SyntaxError) as error:
        raise ValueError("matched property is not a valid quoted string") from error
    if not isinstance(value, str):
        raise ValueError("matched property is not a string")
    return value


def decode_svg_value(value: str) -> bytes:
    value = value.strip()
    if value.startswith("data:image/svg+xml"):
        try:
            metadata, payload = value.split(",", 1)
        except ValueError as error:
            raise ValueError("malformed SVG data URI") from error
        if ";base64" in metadata.lower():
            try:
                return base64.b64decode(payload, validate=True)
            except ValueError as error:
                raise ValueError("invalid base64 SVG data URI") from error
        return unquote_to_bytes(payload)

    match = re.search(r"<svg\b[\s\S]*?</svg\s*>", value, re.IGNORECASE)
    if match is None:
        raise ValueError("property contains neither SVG markup nor an SVG data URI")
    return match.group(0).encode("utf-8")


def extract_property(source: Path, key: str) -> bytes:
    text = source.read_text(encoding="utf-8")
    property_name = rf"(?:{re.escape(key)}|\"{re.escape(key)}\"|'{re.escape(key)}')"
    quoted_value = r'(?P<value>"(?:\\.|[^"\\])*"|\'(?:\\.|[^\'\\])*\')'
    pattern = re.compile(
        rf"(?m)(?:^|[,{{])\s*{property_name}\s*:\s*{quoted_value}\s*,?"
    )
    matches = list(pattern.finditer(text))
    if not matches:
        raise ValueError(f"property not found: {key}")
    svg_values = []
    for match in matches:
        try:
            svg_values.append(
                decode_svg_value(decode_literal(match.group("value")))
            )
        except ValueError:
            continue
    if not svg_values:
        raise ValueError(f"property has no SVG value: {key}")
    if len(svg_values) > 1:
        raise ValueError(
            f"property has multiple SVG values ({len(svg_values)} matches): {key}"
        )
    return svg_values[0]


def convert(args: argparse.Namespace) -> None:
    source = args.source.resolve()
    output = args.output.resolve()
    if not source.is_file():
        raise ValueError(f"source file does not exist: {source}")
    if output.suffix.lower() != ".png":
        raise ValueError(f"output must end in .png: {output}")

    options: dict[str, object] = {"write_to": str(output)}
    if args.width is not None:
        options["output_width"] = args.width
    if args.height is not None:
        options["output_height"] = args.height
    if args.background_color is not None:
        options["background_color"] = args.background_color

    output.parent.mkdir(parents=True, exist_ok=True)
    if args.key:
        options["bytestring"] = extract_property(source, args.key)
    else:
        if source.suffix.lower() != ".svg":
            raise ValueError("use --key when source is not a standalone .svg file")
        options["url"] = str(source)

    cairosvg.svg2png(**options)

    with Image.open(output) as image:
        width, height = image.size
        if args.width is not None and width != args.width:
            raise RuntimeError(f"expected width {args.width}, got {width}")
        if args.height is not None and height != args.height:
            raise RuntimeError(f"expected height {args.height}, got {height}")
        print(f"wrote {output} ({width}x{height}, {image.mode})")


def main() -> int:
    args = parse_args()
    try:
        convert(args)
    except (OSError, ValueError, RuntimeError) as error:
        print(f"svg_to_png.py: {error}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
