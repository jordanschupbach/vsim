#!/usr/bin/env python3
"""Verify the installed runtime can actually run and includes no editor executable."""
import json
from pathlib import Path
import subprocess
import sys
import tempfile

cmake, build, config, graphics = sys.argv[1:5]
with tempfile.TemporaryDirectory(prefix="vsim-install-") as folder:
    prefix = Path(folder)
    subprocess.run([cmake, "--install", build, "--config", config, "--prefix", str(prefix)],
                   check=True, capture_output=True, text=True, timeout=10)
    suffix = ".exe" if sys.platform == "win32" else ""
    binary = prefix / "bin" / ("vsim_runtime" + suffix)
    expected = sorted([binary.name] + (["vsim_game" + suffix] if graphics == "ON" else []))
    if sorted(p.name for p in (prefix / "bin").iterdir()) != expected:
        raise SystemExit("runtime installation contains unexpected executables")
    result = subprocess.run([
        str(binary), "--scene", str(prefix / "share/vsim/scenes/conformance.json"), "--ticks", "120"
    ], check=True, capture_output=True, text=True, timeout=10)
    if json.loads(result.stdout) != {"ok": True, "ticks": 120, "entities": 3}:
        raise SystemExit("installed runtime produced an unexpected result")
    result = subprocess.run([
        str(binary), "--scene", str(prefix / "share/vsim/examples/crystal_courier/scene.json"),
        "--interactive"
    ], input='{"op":"tick","actions":{"move_x":1}}\n',
        check=True, capture_output=True, text=True, timeout=10)
    response = json.loads(result.stdout)
    players = [entry["components"] for entry in response["result"]["entities"]
               if entry["components"]["name"] == "Player"]
    if not response["ok"] or players[0]["transform"]["position"] != [2, 0, 1]:
        raise SystemExit("installed example did not respond to player input")
    if graphics == "ON":
        result = subprocess.run([
            str(prefix / "bin" / ("vsim_game" + suffix)), "--scene",
            str(prefix / "share/vsim/examples/crystal_courier_3d/scene.json"),
            "--offscreen", "--frames", "2", "--validate", "--width", "320", "--height", "240"
        ], check=True, capture_output=True, text=True, timeout=30)
        if json.loads(result.stdout)["validation_errors"] != 0:
            raise SystemExit("installed 3D game failed Vulkan validation")
        for name in ["Jolt/LICENSE", "NVRHI/LICENSE.txt", "Vulkan-Headers/LICENSE.md"]:
            if not (prefix / "share/vsim/licenses" / name).is_file():
                raise SystemExit(f"missing dependency license: {name}")
    print("Installed runtimes passed conformance; no editor installed")
