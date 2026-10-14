#!/usr/bin/env python3
"""Exercise recipe wiring using built executables, without recursively starting builds/tests."""
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

just, editor, runtime, root = sys.argv[1:5]
root = Path(root).resolve()
sys.argv = sys.argv[:1]


class RecipeTests(unittest.TestCase):
    def test_recipe_list_and_dependency_graph(self):
        result = subprocess.run([just, "--list"], cwd=root, text=True, capture_output=True, check=True)
        self.assertIn("game", result.stdout)
        self.assertIn("editor", result.stdout)
        self.assertIn("editor-pane", result.stdout)
        for recipe in ["game", "editor", "test", "runtime", "install", "rebuild"]:
            dry = subprocess.run([just, "--dry-run", recipe], cwd=root, text=True,
                                 capture_output=True, check=True)
            self.assertIn("cmake --build", dry.stderr)
        dry = subprocess.run([just, "--dry-run", "editor-pane"], cwd=root, text=True,
                             capture_output=True, check=True)
        self.assertIn("mep-editor.sh", dry.stderr)
        self.assertNotIn("cmake", dry.stderr)

    def test_rebuild_rediscovers_dependencies_after_environment_change(self):
        with tempfile.TemporaryDirectory(prefix="vsim rebuild ") as folder:
            workspace = Path(folder)
            for name in ["justfile", "CMakePresets.json"]:
                (workspace / name).write_bytes((root / name).read_bytes())
            (workspace / "CMakeLists.txt").write_text('''\
cmake_minimum_required(VERSION 3.25)
project(RebuildRegression NONE)
find_file(SDK_MARKER marker.txt HINTS "$ENV{VSIM_TEST_SDK}" NO_DEFAULT_PATH REQUIRED)
add_custom_target(selected ALL
  COMMAND "${CMAKE_COMMAND}" -E copy "${SDK_MARKER}" "${CMAKE_BINARY_DIR}/selected.txt")
''', encoding="utf-8")
            old_sdk, new_sdk = workspace / "old sdk", workspace / "new sdk"
            for sdk in [old_sdk, new_sdk]:
                sdk.mkdir()
                (sdk / "marker.txt").write_text(sdk.name, encoding="utf-8")
            environment = dict(os.environ, VSIM_TEST_SDK=str(old_sdk))

            def run(recipe, preset):
                result = subprocess.run([just, recipe, preset], cwd=workspace, env=environment,
                                        text=True, capture_output=True, timeout=10)
                self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
                return (workspace / "build" / preset / "selected.txt").read_text(encoding="utf-8")

            for preset in ["debug", "release"]:
                environment["VSIM_TEST_SDK"] = str(old_sdk)
                self.assertEqual(run("build", preset), "old sdk")
                environment["VSIM_TEST_SDK"] = str(new_sdk)
                self.assertEqual(run("build", preset), "old sdk")
                self.assertEqual(run("rebuild", preset), "new sdk")

    def test_recipes_launch_game_and_preloaded_editor_with_literal_paths(self):
        with tempfile.TemporaryDirectory(prefix="vsim just ") as folder:
            workspace = Path(folder)
            (workspace / "justfile").write_bytes((root / "justfile").read_bytes())
            (workspace / "examples").symlink_to(root / "examples", target_is_directory=True)
            binary_dir = workspace / "build/debug"
            binary_dir.mkdir(parents=True)
            (binary_dir / "vsim_editor_cli").symlink_to(Path(editor).resolve())
            (binary_dir / "vsim_runtime").symlink_to(Path(runtime).resolve())
            # Parameters must be passed as arguments, not interpolated into shell code.
            path = workspace / "scene with 'quotes' $(touch SHOULD_NOT_EXIST).json"
            path.write_bytes((root / "examples/crystal_courier/scene.json").read_bytes())
            commands = '\n'.join(json.dumps(r) for r in [
                {"op": "scene"}, {"op": "simulate", "ticks": 1, "actions": {"move_x": 1}},
                {"op": "scene"}, {"op": "undo"}, {"op": "scene"}]) + '\n'
            result = subprocess.run([just, "--no-deps", "editor-cli", str(path)], cwd=workspace,
                                    input=commands, text=True, capture_output=True, timeout=10)
            self.assertEqual(result.returncode, 0, result.stderr)
            responses = [json.loads(line) for line in result.stdout.splitlines()]
            self.assertTrue(all(r["ok"] for r in responses))
            self.assertEqual(responses[0]["result"], responses[4]["result"])
            self.assertNotEqual(responses[0]["result"], responses[2]["result"])
            self.assertFalse((workspace / "SHOULD_NOT_EXIST").exists())
            for recipe in ["game-cli"]:
                result = subprocess.run([just, "--no-deps", recipe], cwd=workspace,
                                        input="q\n", text=True, capture_output=True, timeout=10)
                self.assertEqual(result.returncode, 0, result.stderr)
                self.assertIn("Crystal Courier", result.stdout)
            result = subprocess.run([just, "--no-deps", "runtime", str(path), "2"], cwd=workspace,
                                    text=True, capture_output=True, timeout=10)
            self.assertEqual(result.returncode, 0, result.stderr)
            self.assertEqual(json.loads(result.stdout)["ticks"], 2)


if __name__ == "__main__":
    unittest.main(verbosity=2)
