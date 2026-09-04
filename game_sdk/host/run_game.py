#!/usr/bin/env python3
"""Deterministic host smoke runner for Mosaico game projects.

The browser transport is deliberately kept outside game code. The initial
runner validates the shared C game model and emits a deterministic RGB image;
the GSP Canvas simulator transport can replace this presenter without changing
the project ABI.
"""
from __future__ import annotations

import argparse
import ctypes
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
import json
from pathlib import Path
import struct
import subprocess
import tempfile
import zlib


MAX_BULLETS = 64
MAX_ENEMIES = 32
TOWER_MAX_ENEMIES = 48
TOWER_MAX_PROJECTILES = 64
TOWER_PAD_COUNT = 9

class Actor(ctypes.Structure):
    _fields_ = [("x", ctypes.c_float), ("y", ctypes.c_float),
                ("vx", ctypes.c_float), ("vy", ctypes.c_float),
                ("kind", ctypes.c_uint8), ("active", ctypes.c_bool)]

class Game(ctypes.Structure):
    _fields_ = [("phase", ctypes.c_int), ("player", Actor),
                ("bullets", Actor * MAX_BULLETS),
                ("enemies", Actor * MAX_ENEMIES),
                ("score", ctypes.c_uint32), ("tick", ctypes.c_uint32),
                ("rng", ctypes.c_uint32), ("fire_cooldown", ctypes.c_uint16),
                ("spawn_cooldown", ctypes.c_uint16), ("lives", ctypes.c_uint8)]

class TowerEnemy(ctypes.Structure):
    _fields_ = [("x", ctypes.c_float), ("y", ctypes.c_float),
                ("hp", ctypes.c_float), ("max_hp", ctypes.c_float),
                ("speed", ctypes.c_float), ("waypoint", ctypes.c_uint16),
                ("kind", ctypes.c_uint8), ("slow_ticks", ctypes.c_uint8),
                ("active", ctypes.c_bool)]

class TowerProjectile(ctypes.Structure):
    _fields_ = [("x", ctypes.c_float), ("y", ctypes.c_float),
                ("vx", ctypes.c_float), ("vy", ctypes.c_float),
                ("target", ctypes.c_uint16), ("damage", ctypes.c_uint8),
                ("kind", ctypes.c_uint8), ("ttl", ctypes.c_uint8),
                ("active", ctypes.c_bool)]

class TowerSlot(ctypes.Structure):
    _fields_ = [("x", ctypes.c_int16), ("y", ctypes.c_int16),
                ("cooldown", ctypes.c_uint16), ("type", ctypes.c_uint8),
                ("level", ctypes.c_uint8), ("occupied", ctypes.c_bool)]

class TowerGame(ctypes.Structure):
    _fields_ = [("phase", ctypes.c_int),
                ("enemies", TowerEnemy * TOWER_MAX_ENEMIES),
                ("projectiles", TowerProjectile * TOWER_MAX_PROJECTILES),
                ("towers", TowerSlot * TOWER_PAD_COUNT),
                ("tick", ctypes.c_uint32), ("rng", ctypes.c_uint32),
                ("score", ctypes.c_uint32), ("kills", ctypes.c_uint32),
                ("shots", ctypes.c_uint32), ("credits", ctypes.c_uint16),
                ("wave", ctypes.c_uint16), ("wave_spawned", ctypes.c_uint16),
                ("wave_total", ctypes.c_uint16),
                ("spawn_cooldown", ctypes.c_uint16),
                ("intermission", ctypes.c_uint16), ("base_hp", ctypes.c_uint8),
                ("selected_type", ctypes.c_uint8),
                ("pointer_down", ctypes.c_bool)]

class HostResult(ctypes.Structure):
    _fields_ = [("frames", ctypes.c_uint32), ("wave", ctypes.c_uint32),
                ("score", ctypes.c_uint32), ("credits", ctypes.c_uint32),
                ("base_hp", ctypes.c_uint32), ("kills", ctypes.c_uint32),
                ("state_hash", ctypes.c_uint32)]

class HostEvent(ctypes.Structure):
    _fields_ = [("frame", ctypes.c_uint32), ("type", ctypes.c_uint8),
                ("x", ctypes.c_int16), ("y", ctypes.c_int16)]

EVENT_TYPES = {"tap": 1, "pause": 2, "resume": 3, "step": 4, "reset": 5}

def load_replay(path: Path | None) -> list[dict[str, int | str]]:
    if path is None:
        return []
    value = json.loads(path.read_text(encoding="utf-8"))
    events = value.get("events") if isinstance(value, dict) else value
    if not isinstance(events, list):
        raise ValueError("replay must be an event array or an object containing events")
    normalized = []
    previous = -1
    for item in events:
        if not isinstance(item, dict) or item.get("type") not in EVENT_TYPES:
            raise ValueError("replay contains an unsupported event")
        frame = int(item.get("frame", -1))
        if frame < previous or frame < 0:
            raise ValueError("replay events must use non-decreasing non-negative frames")
        previous = frame
        normalized.append({"frame": frame, "type": str(item["type"]),
                           "x": int(item.get("x", 0)), "y": int(item.get("y", 0))})
    return normalized

def rgb565_png(path: Path, framebuffer: ctypes.Array[ctypes.c_uint16]) -> None:
    width = height = 480
    pixels = bytearray(width * height * 3)
    for index, value in enumerate(framebuffer):
        pixels[index * 3] = ((value >> 11) & 31) * 255 // 31
        pixels[index * 3 + 1] = ((value >> 5) & 63) * 255 // 63
        pixels[index * 3 + 2] = (value & 31) * 255 // 31
    def chunk(kind: bytes, data: bytes) -> bytes:
        return (struct.pack(">I", len(data)) + kind + data +
                struct.pack(">I", zlib.crc32(kind + data) & 0xffffffff))
    rows = b"".join(b"\0" + pixels[y * width * 3:(y + 1) * width * 3]
                    for y in range(height))
    path.write_bytes(b"\x89PNG\r\n\x1a\n" +
        chunk(b"IHDR", struct.pack(">IIBBBBB", width, height, 8, 2, 0, 0, 0)) +
        chunk(b"IDAT", zlib.compress(rows, 9)) + chunk(b"IEND", b""))

def png(path: Path, game: Game) -> None:
    width = height = 480
    pixels = bytearray(bytes((5, 10, 28)) * width * height)
    def rect(x: int, y: int, w: int, h: int, color: tuple[int, int, int]) -> None:
        for py in range(max(0, y), min(height, y + h)):
            for px in range(max(0, x), min(width, x + w)):
                offset = (py * width + px) * 3
                pixels[offset:offset + 3] = bytes(color)
    rect(int(game.player.x), int(game.player.y), 36, 36, (80, 190, 255))
    for actor in game.bullets:
        if actor.active: rect(int(actor.x), int(actor.y), 6, 12, (255, 210, 50))
    colors = ((255, 140, 40), (255, 50, 190), (70, 230, 110))
    for actor in game.enemies:
        if actor.active: rect(int(actor.x), int(actor.y), 28, 28, colors[actor.kind])
    def chunk(kind: bytes, data: bytes) -> bytes:
        return struct.pack(">I", len(data)) + kind + data + struct.pack(">I", zlib.crc32(kind + data) & 0xffffffff)
    rows = b"".join(b"\0" + pixels[y * width * 3:(y + 1) * width * 3] for y in range(height))
    path.write_bytes(b"\x89PNG\r\n\x1a\n" +
        chunk(b"IHDR", struct.pack(">IIBBBBB", width, height, 8, 2, 0, 0, 0)) +
        chunk(b"IDAT", zlib.compress(rows, 9)) + chunk(b"IEND", b""))

def tower_png(path: Path, game: TowerGame) -> None:
    width = height = 480
    pixels = bytearray(bytes((12, 30, 35)) * width * height)
    def rect(x: int, y: int, w: int, h: int, color: tuple[int, int, int]) -> None:
        for py in range(max(0, y), min(height, y + h)):
            start = (py * width + max(0, x)) * 3
            count = max(0, min(width, x + w) - max(0, x))
            pixels[start:start + count*3] = bytes(color) * count
    rect(0, 0, 480, 69, (5, 10, 20))
    rect(0, 65, 480, 4, (13, 72, 83))
    for x, y, w, h in ((0, 82, 149, 44), (104, 82, 44, 131),
                        (104, 168, 273, 44), (332, 168, 44, 133),
                        (72, 256, 305, 44), (72, 256, 44, 127),
                        (72, 338, 408, 44)):
        rect(x, y, w, h, (18, 70, 78))
    for x, y, w, h in ((0, 91, 139, 26), (113, 91, 26, 112),
                        (113, 177, 254, 26), (341, 177, 26, 114),
                        (81, 265, 286, 26), (81, 265, 26, 108),
                        (81, 347, 399, 26)):
        rect(x, y, w, h, (32, 42, 52))
    for x in range(0, 470, 26):
        for y in (101, 187, 275, 357): rect(x, y, 10, 2, (75, 196, 196))
    tower_colors = ((42, 224, 231), (255, 193, 61), (110, 145, 255))
    for tower in game.towers:
        rect(tower.x - 24, tower.y - 20, 48, 44, (3, 12, 17))
        rect(tower.x - 20, tower.y - 20, 40, 40, (15, 42, 48))
        rect(tower.x - 14, tower.y - 14, 28, 28,
             tower_colors[tower.type] if tower.occupied else (24, 63, 68))
        if tower.occupied:
            rect(tower.x - 6, tower.y - 5, 12, 10, (222, 252, 250))
    enemy_colors = ((244, 103, 74), (172, 91, 218), (239, 174, 52))
    for enemy in game.enemies:
        if enemy.active:
            size = 22 if enemy.kind == 1 else 14 if enemy.kind == 2 else 18
            rect(int(enemy.x)-size//2, int(enemy.y)-size//2, size, size,
                 (105, 180, 245) if enemy.slow_ticks else enemy_colors[enemy.kind])
    for projectile in game.projectiles:
        if projectile.active:
            color = tower_colors[projectile.kind]
            rect(int(projectile.x)-2, int(projectile.y)-2, 5, 5, color)
    rect(449, 335, 31, 48, (3, 12, 20))
    rect(454, 330, 26, 48, (21, 63, 75))
    rect(464, 344, 7, 22, (42, 224, 231))
    rect(0, 392, 480, 88, (3, 10, 16))
    for index, x in enumerate((8, 164, 320)):
        rect(x, 402, 146, 67, (19, 47, 56) if game.selected_type == index else (10, 25, 32))
        rect(x+8, 411, 35, 42, (5, 16, 24))
        rect(x+13, 419, 25, 25, tower_colors[index])
    def chunk(kind: bytes, data: bytes) -> bytes:
        return struct.pack(">I", len(data)) + kind + data + struct.pack(">I", zlib.crc32(kind + data) & 0xffffffff)
    rows = b"".join(b"\0" + pixels[y*width*3:(y+1)*width*3] for y in range(height))
    path.write_bytes(b"\x89PNG\r\n\x1a\n" +
        chunk(b"IHDR", struct.pack(">IIBBBBB", width, height, 8, 2, 0, 0, 0)) +
        chunk(b"IDAT", zlib.compress(rows, 9)) + chunk(b"IEND", b""))

def run_tower(source: Path, directory: Path, frames: int, output: Path,
              replay: list[dict[str, int | str]] | None = None) -> dict[str, object]:
    repository = Path(__file__).resolve().parents[2]
    project = source.parents[1]
    library = directory / "tower_game.so"
    sources = [
        repository / "game_sdk/host/tower_host_renderer.c",
        source,
        repository / "game_sdk/components/mosaico_game_2d/mosaico_game_2d.c",
        repository / "game_sdk/components/mosaico_game_tilemap/mosaico_game_tilemap.c",
    ]
    includes = [
        repository / "game_sdk/host/include",
        repository / "game_sdk/components/mosaico_game_assets/include",
        repository / "game_sdk/components/mosaico_game_2d/include",
        repository / "game_sdk/components/mosaico_game_tilemap/include",
        project / "main",
        project / "managed_components/georgik__raylib/include",
        project / "managed_components/georgik__raylib/raylib/src",
    ]
    if not (includes[-1] / "raylib.h").is_file():
        raise RuntimeError("host preview needs project dependencies; run 'game build' first")
    command = ["cc", "-shared", "-fPIC", "-O2", "-std=c11", "-Wall", "-Werror"]
    command.extend(str(item) for item in sources)
    for include in includes:
        command.extend(("-I", str(include)))
    command.extend(("-lm", "-o", str(library)))
    subprocess.run(command, check=True)
    api = ctypes.CDLL(str(library))
    framebuffer = (ctypes.c_uint16 * (480 * 480))()
    result = HostResult()
    api.mosaico_tower_host_render_replay.argtypes = [ctypes.c_char_p, ctypes.c_uint,
        ctypes.POINTER(HostEvent), ctypes.c_size_t, ctypes.POINTER(ctypes.c_uint16),
        ctypes.POINTER(HostResult)]
    api.mosaico_tower_host_render_replay.restype = ctypes.c_int
    assets = project / "assets/generated"
    values = replay or []
    encoded = (HostEvent * len(values))(*[
        HostEvent(int(item["frame"]), EVENT_TYPES[str(item["type"])],
                  int(item["x"]), int(item["y"])) for item in values])
    status = api.mosaico_tower_host_render_replay(str(assets).encode(), max(0, frames),
        encoded if values else None, len(values), framebuffer, ctypes.byref(result))
    if status:
        raise RuntimeError(f"shared C renderer failed: {status}")
    output.parent.mkdir(parents=True, exist_ok=True)
    rgb565_png(output, framebuffer)
    return {"frames": result.frames, "wave": result.wave, "score": result.score,
            "credits": result.credits, "base_hp": result.base_hp,
            "kills": result.kills,
            "state_hash": f"{result.state_hash:08x}",
            "frame": str(output)}

def serve_preview(listen: str, port: int, frame: Path,
                  metadata: dict[str, object]) -> None:
    page = f"""<!doctype html><html><head><meta charset=utf-8>
<title>Mosaico game preview</title><style>
body{{margin:0;background:#050a12;color:#d9ffff;font:14px system-ui;display:grid;
place-items:center;min-height:100vh}}main{{padding:22px;background:#091722;border:1px solid #1c7b89;
box-shadow:0 18px 80px #000;border-radius:16px}}img{{width:min(78vh,480px);image-rendering:pixelated;
display:block;border:1px solid #299fad}}pre{{color:#82cbd1;margin:12px 0 0}}
</style></head><body><main><img src=/frame.png><pre>{json.dumps(metadata, indent=2)}</pre>
</main></body></html>""".encode()
    image = frame.read_bytes()
    class Handler(BaseHTTPRequestHandler):
        def do_GET(self) -> None:
            body, content_type = ((image, "image/png") if self.path.startswith("/frame.png")
                                  else (page, "text/html; charset=utf-8"))
            self.send_response(200)
            self.send_header("Content-Type", content_type)
            self.send_header("Content-Length", str(len(body)))
            self.send_header("Cache-Control", "no-store")
            self.end_headers()
            self.wfile.write(body)
        def log_message(self, fmt: str, *args: object) -> None:
            return
    print(json.dumps({"preview_url": f"http://{listen}:{port}/", **metadata}), flush=True)
    ThreadingHTTPServer((listen, port), Handler).serve_forever()

def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--project", type=Path, required=True)
    parser.add_argument("--headless", action="store_true")
    parser.add_argument("--frames", type=int, default=300)
    parser.add_argument("--listen", default="127.0.0.1")
    parser.add_argument("--port", type=int, default=8460)
    parser.add_argument("--replay", type=Path)
    parser.add_argument("--state-output", type=Path)
    args = parser.parse_args()
    shooter_source = args.project / "main" / "shooter_game.c"
    tower_source = args.project / "main" / "tower_game.c"
    if not shooter_source.is_file() and not tower_source.is_file():
        parser.error("project does not expose a supported host game model")
    with tempfile.TemporaryDirectory(prefix="mosaico-game-") as directory:
        output = args.project / "build-host" / "frame.png"
        if tower_source.is_file():
            replay = load_replay(args.replay)
            result = run_tower(tower_source, Path(directory), args.frames, output,
                               replay if args.replay else None)
            if args.state_output:
                args.state_output.parent.mkdir(parents=True, exist_ok=True)
                args.state_output.write_text(json.dumps(result, indent=2, sort_keys=True) + "\n",
                                             encoding="utf-8")
            if args.headless:
                print(json.dumps(result))
            else:
                serve_preview(args.listen, args.port, output.resolve(), result)
            return 0
        library = Path(directory) / "game.so"
        subprocess.run(["cc", "-shared", "-fPIC", "-O2", str(shooter_source), "-lm", "-o", str(library)], check=True)
        api = ctypes.CDLL(str(library))
        api.shooter_game_reset.argtypes = [ctypes.POINTER(Game), ctypes.c_uint32]
        api.shooter_game_set_pointer.argtypes = [ctypes.POINTER(Game), ctypes.c_float, ctypes.c_float, ctypes.c_bool]
        api.shooter_game_update.argtypes = [ctypes.POINTER(Game)]
        api.shooter_game_state_hash.argtypes = [ctypes.POINTER(Game)]
        api.shooter_game_state_hash.restype = ctypes.c_uint32
        game = Game()
        api.shooter_game_reset(ctypes.byref(game), 0x4D4F5341)
        api.shooter_game_set_pointer(ctypes.byref(game), 240, 420, True)
        for index in range(max(0, args.frames)):
            x = 240 + ((index // 30) % 5 - 2) * 55
            api.shooter_game_set_pointer(ctypes.byref(game), x, 420, True)
            api.shooter_game_update(ctypes.byref(game))
        output.parent.mkdir(parents=True, exist_ok=True)
        png(output, game)
        print(json.dumps({"frames": args.frames, "score": game.score,
            "lives": game.lives, "state_hash": f"{api.shooter_game_state_hash(ctypes.byref(game)):08x}",
            "frame": str(output)}))
    return 0

if __name__ == "__main__":
    raise SystemExit(main())
