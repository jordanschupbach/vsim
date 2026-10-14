#!/usr/bin/env python3
"""Render the actual editor, drive its model, and exercise the public recipe."""
import json
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

editor, just, root = sys.argv[1:4]
root = Path(root).resolve()
sys.argv = sys.argv[:1]


class EditorGuiTests(unittest.TestCase):
    def test_editor_recipe_renders_and_automation_preserves_authored_scene(self):
        with tempfile.TemporaryDirectory(prefix="vsim editor ") as directory:
            workspace = Path(directory)
            (workspace / "justfile").write_bytes((root / "justfile").read_bytes())
            binary_dir = workspace / "build/debug"
            binary_dir.mkdir(parents=True)
            (binary_dir / "vsim_editor").symlink_to(Path(editor).resolve())
            scene = workspace / "scene 'quoted' $(touch SHOULD_NOT_EXIST).json"
            scene.write_bytes((root / "examples/crystal_courier_3d/scene.json").read_bytes())
            saved, capture = workspace / "saved.json", workspace / "editor.ppm"
            commands = workspace / "commands.json"
            commands.write_text(json.dumps([
                {"frame": 0, "request": {"op": "select", "entity_id": 2}},
                {"frame": 0, "request": {"op": "scene"}},
                {"frame": 2, "request": {"op": "play"}},
                {"frame": 6, "request": {"op": "play_scene"}},
                {"frame": 6, "request": {"op": "stop"}},
                {"frame": 6, "request": {"op": "scene"}},
                {"frame": 7, "request": {"op": "save", "path": str(saved)}},
            ]))
            result = subprocess.run([
                just, "--no-deps", "editor", str(scene), "--validate", "--offscreen",
                "--frames", "10", "--capture", str(capture), "--commands", str(commands),
            ], cwd=workspace, input="", text=True, capture_output=True, timeout=60)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            responses = [json.loads(line) for line in result.stdout.splitlines()]
            self.assertEqual(responses[-1]["frames"], 10)
            self.assertEqual(responses[-1]["validation_errors"], 0)
            self.assertFalse(responses[-1]["status"]["playing"])
            self.assertFalse(responses[-1]["status"]["dirty"])
            self.assertEqual(responses[1]["result"], responses[5]["result"])
            self.assertNotEqual(responses[1]["result"], responses[3]["result"])
            def camera(document):
                return next(e["components"] for e in document["entities"]
                            if e["components"]["name"] == "Camera")

            authored_camera = camera(responses[1]["result"])
            playing_camera = camera(responses[3]["result"])
            self.assertNotEqual(authored_camera["transform"], playing_camera["transform"])
            self.assertEqual(authored_camera["camera"], playing_camera["camera"])
            self.assertEqual(authored_camera["script"], playing_camera["script"])
            self.assertEqual(responses[1]["result"], json.loads(saved.read_text()))
            self.assertFalse((workspace / "SHOULD_NOT_EXIST").exists())
            with capture.open("rb") as image:
                self.assertEqual(image.readline(), b"P6\n")
                width, height = map(int, image.readline().split())
                self.assertEqual(image.readline(), b"255\n")
                pixels = image.read()
            self.assertEqual((width, height), (1280, 800))
            self.assertEqual(len(pixels), width * height * 3)

            def region(x1, y1, x2, y2):
                return [tuple(pixels[(y * width + x) * 3:(y * width + x) * 3 + 3])
                        for y in range(y1, y2) for x in range(x1, x2)]

            # Text and anti-aliasing must actually reach the framebuffer; the
            # viewport must contain shaded geometry, not just flat panel colors.
            header = region(0, 0, width, 80)
            self.assertGreater(sum(min(color) > 180 for color in header), 100)
            self.assertGreater(len(set(region(240, 100, 940, 760))), 200)
            self.assertGreater(len(set(region(960, 100, 1270, 650))), 20)

    def test_empty_scene_has_editor_camera_and_renders(self):
        result = subprocess.run([editor, "--offscreen", "--frames", "3", "--validate"],
                                input="", text=True, capture_output=True, timeout=60)
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(json.loads(result.stdout.splitlines()[-1])["validation_errors"], 0)

    def test_invalid_arguments_and_automation_fail_before_gpu_creation(self):
        for arguments in [["--offscreen"], ["--frames", "0"], ["--frames", "2.5"],
                          ["--frames", "-1"], ["--frames", "100001"], ["--unknown", "x"],
                          ["--scene"], ["--validate", "--validate"], ["--x11", "--x11"]]:
            result = subprocess.run([editor, *arguments], input="", text=True,
                                    capture_output=True, timeout=5)
            self.assertEqual(result.returncode, 1, result.stderr)
            self.assertTrue(result.stderr.startswith("vsim_editor: "))
        result = subprocess.run([editor, "--x11", "--offscreen", "--frames", "1"],
                                input="", text=True, capture_output=True, timeout=5)
        self.assertEqual(result.returncode, 1)
        self.assertEqual(result.stderr.strip(), "vsim_editor: --x11 cannot be combined with --offscreen")
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "commands.json"
            for commands in [{}, [{"frame": 3, "request": {"op": "scene"}}],
                             [{"frame": -1, "request": {"op": "scene"}}],
                             [{"frame": 0, "request": []}],
                             [{"frame": 1, "request": {}}, {"frame": 0, "request": {}}]]:
                path.write_text(json.dumps(commands))
                result = subprocess.run([editor, "--offscreen", "--frames", "3", "--commands", str(path)],
                                        input="", text=True, capture_output=True, timeout=5)
                self.assertEqual(result.returncode, 1, result.stderr)
                self.assertTrue(result.stderr.startswith("vsim_editor: "))


if __name__ == "__main__":
    unittest.main(verbosity=2)
