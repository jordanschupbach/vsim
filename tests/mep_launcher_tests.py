#!/usr/bin/env python3
"""Check relocation, argument forwarding, and the child process contract with Mep."""
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import unittest

root = Path(sys.argv[1]).resolve()
sys.argv = sys.argv[:1]


class MepLauncherTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory(prefix="vsim mep ")
        self.addCleanup(self.temporary.cleanup)
        self.workspace = Path(self.temporary.name)
        self.engine = self.workspace / "engines" / "VSim 'quoted' $(touch UNEXPECTED)"
        scripts = self.engine / "scripts"
        scripts.mkdir(parents=True)
        self.launcher = scripts / "mep-editor.sh"
        shutil.copyfile(root / "scripts/mep-editor.sh", self.launcher)
        self.editor = self.engine / "build/debug/vsim_editor"
        self.editor.parent.mkdir(parents=True)
        # A process probe records observable launch behavior without requiring X11.
        self.editor.write_text(f"#!{sys.executable}\n" + '''\
import json, os, sys
print(json.dumps({"argv": sys.argv[1:], "cwd": os.getcwd(), "pid": os.getpid()}))
sys.exit(int(os.environ.get("VSIM_LAUNCHER_TEST_EXIT", "0")))
''', encoding="utf-8")
        self.editor.chmod(0o755)

    def launch(self, *arguments, exit_code=0):
        with subprocess.Popen(["bash", str(self.launcher), *arguments], cwd=self.workspace,
                              env=dict(os.environ, VSIM_LAUNCHER_TEST_EXIT=str(exit_code)),
                              text=True, stdout=subprocess.PIPE, stderr=subprocess.PIPE) as child:
            try:
                output, errors = child.communicate(timeout=5)
            except subprocess.TimeoutExpired:
                child.kill()
                child.communicate()
                raise
            self.assertEqual(child.returncode, exit_code, errors)
            result = json.loads(output)
            # exec preserves the PID so Mep can adopt and own the editor process.
            self.assertEqual(result["pid"], child.pid)
            self.assertEqual(result["cwd"], str(self.workspace))
            self.assertFalse((self.workspace / "UNEXPECTED").exists())
            return result["argv"]

    def test_default_scene_resolves_after_relocation_and_retains_workspace(self):
        self.assertEqual(self.launch(), ["--x11", "--scene", str(
            self.engine / "examples/crystal_courier_3d/scene.json")])

    def test_flags_keep_default_scene_and_preserve_exit_status(self):
        self.assertEqual(self.launch("--validate", exit_code=7), [
            "--x11", "--scene", str(self.engine / "examples/crystal_courier_3d/scene.json"),
            "--validate"])

    def test_scene_and_capture_paths_are_literal_workspace_relative_arguments(self):
        scene = "scenes/my 'scene' $(touch UNEXPECTED).json"
        capture = "images/`touch UNEXPECTED`.ppm"
        self.assertEqual(self.launch(scene, "--validate", "--capture", capture),
                         ["--x11", "--scene", scene, "--validate", "--capture", capture])

    def test_missing_binary_reports_build_command_without_starting_a_build(self):
        self.editor.unlink()
        result = subprocess.run(["bash", str(self.launcher)], cwd=self.workspace,
                                text=True, capture_output=True, timeout=5)
        self.assertEqual(result.returncode, 1)
        self.assertEqual(result.stdout, "")
        self.assertIn(str(self.engine), result.stderr)
        self.assertIn("nix develop path:. --command just build-editor", result.stderr)
        self.assertEqual(list(self.editor.parent.iterdir()), [])


if __name__ == "__main__":
    unittest.main(verbosity=2)
