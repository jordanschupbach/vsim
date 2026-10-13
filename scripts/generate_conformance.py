#!/usr/bin/env python3
"""Keep the embedded, exportable conformance scene in sync with its Lua source."""
import argparse
import json
from pathlib import Path

root = Path(__file__).resolve().parents[1]
parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument("--check", action="store_true")
args = parser.parse_args()
document = {
    "version": 1,
    "next_id": 3,
    "entities": [
        {"id": 1, "components": {"name": "API conformance", "script": {
            "source": (root / "scenes/conformance.lua").read_text(encoding="utf-8")}}},
        {"id": 2, "components": {"name": "Camera", "camera": {}, "transform": {
            "position": [0, 2, 5], "rotation": [1, 0, 0, 0], "scale": [1, 1, 1]}}},
    ],
    "prefabs": {"CameraPrefab": {"name": "Prefab camera", "camera": {}}},
}
text = json.dumps(document, indent=2, ensure_ascii=False) + "\n"
destination = root / "scenes/conformance.json"
if args.check:
    if destination.read_text(encoding="utf-8") != text:
        raise SystemExit("conformance.json is stale; run scripts/generate_conformance.py")
else:
    destination.write_text(text, encoding="utf-8")
