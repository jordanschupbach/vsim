#!/usr/bin/env python3
"""Exercise the graphical executable and actual game recipes with Vulkan readback."""
import json
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

game, just, root = sys.argv[1:4]
game, root = Path(game).resolve(), Path(root).resolve()
sys.argv = sys.argv[:1]


class GraphicalGameTests(unittest.TestCase):
    def test_invalid_arguments_fail_with_diagnostics(self):
        for args in [[], ["--scene"], ["--unknown", "x"], ["--scene", "missing"],
                     ["--scene", "missing", "--frames", "0"], ["--frames", "-1"],
                     ["--frames", "2.5"], ["--offscreen", "--offscreen"],
                     ["--scene", "missing", "--offscreen"]]:
            result = subprocess.run([game, *args], text=True, capture_output=True, timeout=10)
            self.assertEqual(result.returncode, 1, result.stderr)
            self.assertTrue(result.stderr.startswith("vsim_game: "), result.stderr)

    def test_game_and_run_recipes_render_and_forward_literal_paths(self):
        with tempfile.TemporaryDirectory(prefix="vsim graphical ") as folder:
            workspace = Path(folder)
            (workspace / "justfile").write_bytes((root / "justfile").read_bytes())
            (workspace / "examples").symlink_to(root / "examples", target_is_directory=True)
            binary = workspace / "build/debug"
            binary.mkdir(parents=True)
            (binary / "vsim_game").symlink_to(game)
            for recipe in ["game", "run"]:
                capture = workspace / f"{recipe} 'capture' $(touch SHOULD_NOT_EXIST).ppm"
                scene = workspace / f"{recipe} state.json"
                result = subprocess.run([
                    just, "--no-deps", recipe, "--offscreen", "--frames", "4", "--validate",
                    "--width", "320", "--height", "240", "--capture", str(capture),
                    "--output", str(scene)
                ], cwd=workspace, text=True, capture_output=True, timeout=30)
                self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
                report = json.loads(result.stdout)
                self.assertEqual(report["frames"], 4)
                self.assertEqual(report["renderer"], "NVRHI/Vulkan")
                self.assertEqual(report["physics"], "Jolt")
                self.assertEqual(report["validation_errors"], 0)
                data = capture.read_bytes()
                self.assertTrue(data.startswith(b"P6\n320 240\n255\n"))
                self.assertEqual(len(data.split(b"\n", 3)[3]), 320*240*3)
                player = next(e["components"] for e in json.loads(scene.read_text())["entities"]
                              if e["components"]["name"] == "Player")
                self.assertLess(player["transform"]["position"][1], 1.1)
                self.assertFalse((workspace / "SHOULD_NOT_EXIST").exists())


if __name__ == "__main__":
    unittest.main(verbosity=2)
