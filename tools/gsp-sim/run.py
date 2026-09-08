#!/usr/bin/env python3
"""Pack a GSP scene and preview it on the host.

Interactive mode builds the application WASM (portable C linked against
official Component Registry ``espressif/esp-gsp`` 1.0.0) and serves it.
Headless mode still uses the official ESP-GSP 1.1.0 ``sim`` for PPM dumps.
"""

from __future__ import annotations

import argparse
import functools
import http.server
import os
import shutil
import socket
import subprocess
import sys
import tempfile
import webbrowser
from pathlib import Path

TOOLS_DIR = Path(__file__).resolve().parent
REPO_ROOT = TOOLS_DIR.parents[1]
GSP_ROOT = REPO_ROOT / "submodule" / "esp-gsp"
WEB_DIR = TOOLS_DIR / "web"
VENDOR_DIR = WEB_DIR / "vendor"
BUILD_WEB_DIR = TOOLS_DIR / "build-web"
BUILD_WASM_SH = TOOLS_DIR / "scripts" / "build_wasm.sh"
WEB_CHROME_FILES = ("sideboards.css", "sideboards.js")
DEFAULT_SCENE = REPO_ROOT / "projects" / "gsp_hello" / "ui" / "main.json"
DEFAULT_WASM_HOST = os.environ.get("MOSAIC_WASM_HOST", "0.0.0.0")
DEFAULT_WASM_PORT = int(os.environ.get("MOSAIC_WASM_PORT", "8877"))

sys.path.insert(0, str(TOOLS_DIR))
from fetch_gspc import resolve_gspc, resolve_sdk, resolve_sim  # noqa: E402


def pack_scene(scene: Path, output: Path, gspc: Path) -> None:
    command = [
        str(gspc),
        "pack",
        str(scene),
        "--pixel-format",
        "rgb565",
        "--deployable",
        "-o",
        str(output),
    ]
    subprocess.run(command, check=True)


def lan_url(port: int) -> str | None:
    try:
        with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as probe:
            probe.connect(("8.8.8.8", 80))
            address = probe.getsockname()[0]
    except OSError:
        return None
    if address.startswith("127."):
        return None
    return f"http://{address}:{port}/"


def app_dir_for_scene(scene: Path) -> Path | None:
    if scene.parent.name == "ui":
        candidate = scene.parent.parent / "app"
        if candidate.is_dir():
            return candidate
    return None


def build_app_wasm(bundle: Path, app_dir: Path) -> Path:
    if not BUILD_WASM_SH.is_file():
        raise SystemExit(f"missing {BUILD_WASM_SH}")
    # Concurrent previews of different applications must not overwrite the
    # WASM module or preloaded scene being served by another session.
    build_dir = BUILD_WEB_DIR / app_dir.parent.name
    scene_dir = build_dir / "scene"
    scene_dir.mkdir(parents=True, exist_ok=True)
    staged_bundle = scene_dir / "preview.gspb"
    if bundle.resolve() != staged_bundle.resolve():
        shutil.copy2(bundle, staged_bundle)
    environment = os.environ.copy()
    environment["GSP_APP_BUNDLE"] = str(staged_bundle)
    environment["GSP_APP_DIR"] = str(app_dir)
    environment["GSP_APP_WASM_BUILD_DIR"] = str(build_dir)
    subprocess.run(["bash", str(BUILD_WASM_SH)], check=True, env=environment)
    page = build_dir / "gsp_app_sim.html"
    if not page.is_file():
        raise SystemExit(f"WASM host did not emit {page}")
    shutil.copy2(page, build_dir / "index.html")
    copy_web_chrome(build_dir)
    return build_dir


def copy_web_chrome(staging: Path) -> None:
    for name in WEB_CHROME_FILES:
        source = WEB_DIR / name
        if source.is_file():
            shutil.copy2(source, staging / name)
    assets = WEB_DIR / "assets"
    if assets.is_dir():
        shutil.copytree(assets, staging / "assets", dirs_exist_ok=True)


def serve_wasm_preview(staging: Path, host: str, port: int) -> int:
    handler = functools.partial(
        http.server.SimpleHTTPRequestHandler, directory=str(staging)
    )
    try:
        server = http.server.ThreadingHTTPServer((host, port), handler)
    except OSError as error:
        raise SystemExit(f"cannot bind WASM preview on {host}:{port}: {error}") from error

    local = f"http://127.0.0.1:{port}/"
    remote = lan_url(port)
    print(f"GSP WebAssembly preview is listening on {host}:{port}", flush=True)
    print(f"Open: {local}", flush=True)
    if host in {"0.0.0.0", "::"} and remote:
        print(f"LAN:  {remote}", flush=True)
    print("Press Ctrl-C to stop.", flush=True)
    if os.environ.get("DISPLAY") and host != "127.0.0.1":
        try:
            webbrowser.open(local)
        except Exception:
            pass
    try:
        server.serve_forever()
    except KeyboardInterrupt:
        print()
        return 0
    finally:
        server.server_close()
    return 0


def run_native_sim(bundle: Path, sim_args: list[str]) -> int:
    simulator = resolve_sim()
    os.environ["GSP_SIM_EXECUTABLE"] = str(simulator)
    return subprocess.call([str(simulator), "--bundle", str(bundle), *sim_args])


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "scene",
        nargs="?",
        type=Path,
        default=DEFAULT_SCENE,
        help="scene JSON or precompiled .gspb (default: projects/gsp_hello/ui/main.json)",
    )
    parser.add_argument("--headless", action="store_true")
    parser.add_argument("--interactive", action="store_true")
    parser.add_argument(
        "--native",
        action="store_true",
        help="use the official 1.1.0 native sim browser preview instead of WASM",
    )
    parser.add_argument(
        "--scene-player",
        action="store_true",
        help="serve the scene-only gsp_sim.js player instead of compiling app C",
    )
    parser.add_argument("--frames", type=int)
    parser.add_argument("--fps", type=int, default=60)
    parser.add_argument("--dump-ppm", type=Path)
    parser.add_argument("--host", default=DEFAULT_WASM_HOST)
    parser.add_argument("--port", type=int, default=DEFAULT_WASM_PORT)
    parser.add_argument(
        "sim_args",
        nargs=argparse.REMAINDER,
        help="extra native simulator flags; pass after -- (for example -- --drag 80 360 400 360)",
    )
    args = parser.parse_args()
    scene = args.scene.expanduser().resolve()
    if not scene.is_file():
        raise SystemExit(f"scene not found: {scene}")
    if args.headless and args.interactive:
        raise SystemExit("use either --headless or --interactive")
    if args.headless and args.native:
        raise SystemExit("--native is only for the interactive browser preview")
    if not GSP_ROOT.is_dir():
        raise SystemExit(
            f"ESP-GSP submodule is missing at {GSP_ROOT}; "
            "run git submodule update --init submodule/esp-gsp"
        )

    extra = list(args.sim_args)
    if extra and extra[0] == "--":
        extra = extra[1:]

    use_wasm = not args.headless and not args.native
    if use_wasm and extra:
        raise SystemExit(
            "extra simulator flags require --headless or --native; "
            "WASM preview only serves the packed scene"
        )

    gspc = resolve_gspc()
    os.environ["GSPC_EXECUTABLE"] = str(gspc)
    if use_wasm and not args.scene_player:
        os.environ["ESP_GSP_SDK_ROOT"] = str(resolve_sdk())

    with tempfile.TemporaryDirectory(prefix="mosaico-gsp-sim-") as directory:
        if scene.suffix == ".gspb":
            bundle = scene
        else:
            bundle = Path(directory) / "preview.gspb"
            pack_scene(scene, bundle, gspc)

        if use_wasm:
            if args.scene_player:
                staging = Path(directory) / "wasm"
                staging.mkdir()
                require_vendor = (
                    (VENDOR_DIR / "gsp_sim.js").is_file()
                    and (VENDOR_DIR / "gsp_sim.wasm").is_file()
                    and (WEB_DIR / "index.html").is_file()
                )
                if not require_vendor:
                    raise SystemExit("scene-only player files are missing under tools/gsp-sim/web/")
                shutil.copy2(WEB_DIR / "index.html", staging / "index.html")
                shutil.copy2(WEB_DIR / "player.js", staging / "player.js")
                shutil.copy2(VENDOR_DIR / "gsp_sim.js", staging / "gsp_sim.js")
                shutil.copy2(VENDOR_DIR / "gsp_sim.wasm", staging / "gsp_sim.wasm")
                shutil.copy2(bundle, staging / "preview.gspb")
                copy_web_chrome(staging)
                backend = scene.parent / "sim_backend.json"
                if backend.is_file():
                    shutil.copy2(backend, staging / "backend.json")
                return serve_wasm_preview(staging, args.host, args.port)
            app_dir = app_dir_for_scene(scene)
            if app_dir is None:
                raise SystemExit(
                    f"no portable app C next to {scene}; expected "
                    f"{scene.parent.parent / 'app'} (gsp_app_start)"
                )
            staging = build_app_wasm(bundle, app_dir)
            return serve_wasm_preview(staging, args.host, args.port)

        sim_args: list[str] = []
        if args.headless:
            sim_args.append("--headless")
            if args.frames is None:
                sim_args.extend(["--frames", "30"])
        elif args.frames is None:
            sim_args.extend(["--frames", "0"])
        if args.frames is not None:
            sim_args.extend(["--frames", str(args.frames)])
        if args.fps:
            sim_args.extend(["--fps", str(args.fps)])
        if args.dump_ppm is not None:
            dump = args.dump_ppm.expanduser().resolve()
            dump.parent.mkdir(parents=True, exist_ok=True)
            sim_args.extend(["--dump", str(dump), "--dump-format", "ppm"])
        sim_args.extend(extra)
        return run_native_sim(bundle, sim_args)


if __name__ == "__main__":
    raise SystemExit(main())
