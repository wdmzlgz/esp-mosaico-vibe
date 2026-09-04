from __future__ import annotations

import hashlib
import json
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[2]
PROJECT = ROOT / "projects/tower_defense"


class TowerHostRunnerTests(unittest.TestCase):
    @unittest.skipUnless(
        (PROJECT / "managed_components/georgik__raylib/raylib/src/raylib.h").is_file(),
        "run game build first to resolve the pinned Raylib component",
    )
    def test_state_and_rgb565_screenshot_are_deterministic(self) -> None:
        command = [sys.executable, str(ROOT / "game_sdk/host/run_game.py"),
                   "--project", str(PROJECT), "--headless", "--frames", "300"]
        first = json.loads(subprocess.check_output(command, cwd=ROOT))
        first_png = Path(first["frame"])
        if not first_png.is_absolute(): first_png = ROOT / first_png
        first_hash = hashlib.sha256(first_png.read_bytes()).hexdigest()
        second = json.loads(subprocess.check_output(command, cwd=ROOT))
        second_png = Path(second["frame"])
        if not second_png.is_absolute(): second_png = ROOT / second_png
        self.assertEqual(first["state_hash"], second["state_hash"])
        self.assertEqual(first_hash, hashlib.sha256(second_png.read_bytes()).hexdigest())
        self.assertEqual(first["state_hash"], "589740aa")
        self.assertEqual(first_hash,
                         "fa9995748f1e57de8ee64312ac4563521a075c25a46fc0d070116c85adfe91e0")

    def test_replay_supports_pause_single_step_and_state_output(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            replay = root / "replay.json"
            state = root / "state.json"
            replay.write_text(json.dumps({"events": [
                {"frame": 0, "type": "tap", "x": 240, "y": 220},
                {"frame": 5, "type": "pause"},
                {"frame": 10, "type": "step"},
                {"frame": 20, "type": "resume"},
            ]}), encoding="utf-8")
            command = [sys.executable, str(ROOT / "game_sdk/host/run_game.py"),
                       "--project", str(PROJECT), "--headless", "--frames", "30",
                       "--replay", str(replay), "--state-output", str(state)]
            result = json.loads(subprocess.check_output(command, cwd=ROOT))
            persisted = json.loads(state.read_text(encoding="utf-8"))
            self.assertEqual(result["state_hash"], persisted["state_hash"])
            self.assertEqual(result["frames"], 30)
            self.assertTrue(Path(result["frame"]).is_file())


if __name__ == "__main__":
    unittest.main()
