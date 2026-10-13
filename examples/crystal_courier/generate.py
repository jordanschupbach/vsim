#!/usr/bin/env python3
"""Embed the authored map and Lua behavior in the standalone VSim scene."""
import argparse
import json
from pathlib import Path


def generate(root):
    rows = (root / "layout.txt").read_text(encoding="utf-8").splitlines()
    if not rows or any(len(row) != len(rows[0]) for row in rows):
        raise ValueError("layout must be a nonempty rectangle")
    if len(rows) > 128 or len(rows[0]) > 128:
        raise ValueError("terminal layouts are limited to 128 by 128 tiles")
    text = "".join(rows)
    if set(text) - set("#P.*EX") or text.count("P") != 1 or text.count("E") != 1:
        raise ValueError("layout needs one P, one E, and only #P.*EX tiles")
    if not rows[0] or any(c != "#" for c in rows[0] + rows[-1]) or any(
            row[0] != "#" or row[-1] != "#" for row in rows):
        raise ValueError("layout must be enclosed by walls")
    names = {"#": "Wall", "P": "Player", "*": "Crystal", "E": "Exit", "X": "Trap"}
    components = [
        {"name": "GameController", "script": {"source": (root / "game.lua").read_text(encoding="utf-8")}},
        {"name": "Status: Ready"},
        {"name": "Camera", "camera": {}, "transform": {
            "position": [4, 12, 3], "rotation": [0.70710678, -0.70710678, 0, 0]}},
    ]
    for z, row in enumerate(rows):
        for x, tile in enumerate(row):
            if tile in names:
                components.append({"name": names[tile], "transform": {"position": [x, 0, z]}})
    if len(components) > 10000:
        raise ValueError("layout exceeds the engine limit of 10000 entities")
    return {"version": 1, "next_id": len(components) + 1,
            "entities": [{"id": i, "components": c} for i, c in enumerate(components, 1)],
            "prefabs": {"Trophy": {"name": "Trophy"}}}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--check", action="store_true")
    args = parser.parse_args()
    root = Path(__file__).resolve().parent
    document = json.dumps(generate(root), indent=2, ensure_ascii=False) + "\n"
    path = root / "scene.json"
    if args.check:
        if path.read_text(encoding="utf-8") != document:
            raise SystemExit("Crystal Courier scene is stale; run just generate")
    else:
        path.write_text(document, encoding="utf-8")


if __name__ == "__main__":
    main()
