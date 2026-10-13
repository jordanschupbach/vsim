#!/usr/bin/env python3
"""Terminal presentation/input for Crystal Courier; game rules execute in VSim/Lua."""
import argparse
from contextlib import contextmanager
import json
from pathlib import Path
import subprocess
import sys


@contextmanager
def runtime_session(binary, scene):
    process = subprocess.Popen([str(binary), "--scene", str(scene), "--interactive"],
                               stdin=subprocess.PIPE, stdout=subprocess.PIPE,
                               text=True, encoding="utf-8", bufsize=1)
    failed = False
    try:
        yield process
    except BaseException:
        failed = True
        raise
    finally:
        try:
            process.stdin.close()
        except BrokenPipeError:
            pass
        try:
            code = process.wait(timeout=5)
        except subprocess.TimeoutExpired:
            process.kill()
            process.wait()
            code = -1
        process.stdout.close()
        if code != 0 and not failed:
            raise RuntimeError(f"VSim runtime exited with status {code}")


def tick(process, actions):
    process.stdin.write(json.dumps({"op": "tick", "actions": actions}) + "\n")
    process.stdin.flush()
    line = process.stdout.readline()
    if not line:
        raise RuntimeError("VSim runtime closed before returning a scene")
    response = json.loads(line)
    if not response["ok"]:
        raise RuntimeError(response["error"])
    return response["result"]


def draw(scene):
    symbols = {"Wall": "#", "Crystal": "*", "Trap": "X", "Exit": "E", "Trophy": "!", "Player": "@"}
    entities = [entry["components"] for entry in scene["entities"]]
    tiles = [entity for entity in entities if entity["name"] in symbols]
    if not tiles:
        raise ValueError("scene contains no Crystal Courier tiles")
    for entity in tiles:
        x, _, z = entity["transform"]["position"]
        if any(value != int(value) or not 0 <= value < 128 for value in (x, z)):
            raise ValueError("terminal tiles need integer X/Z coordinates between 0 and 127")
    width = 1 + max(int(e["transform"]["position"][0]) for e in tiles)
    height = 1 + max(int(e["transform"]["position"][2]) for e in tiles)
    board = [["." for _ in range(width)] for _ in range(height)]
    # Draw objects before the player so @ remains visible at the exit/trophy.
    for entity in sorted(tiles, key=lambda e: e["name"] == "Player"):
        x, _, z = entity["transform"]["position"]
        board[int(z)][int(x)] = symbols[entity["name"]]
    print("\n" + "\n".join("".join(row) for row in board))
    for entity in entities:
        if entity["name"].startswith("Status: "):
            print(entity["name"][8:])


def play(binary, scene):
    directions = {"w": {"move_z": -1}, "s": {"move_z": 1},
                  "a": {"move_x": -1}, "d": {"move_x": 1}}
    print("Crystal Courier — a Lua game running in VSim")
    print("@ you | * crystal | # wall | X trap | E exit")
    print("W/A/S/D then Enter to move. R restarts. Q quits.")
    restart = True
    while restart:
        restart = False
        with runtime_session(binary, scene) as process:
            draw(tick(process, {}))
            while True:
                try:
                    command = input("Move [W/A/S/D/R/Q]> ").strip().lower()
                except EOFError:
                    return
                if command == "q":
                    return
                if command == "r":
                    restart = True
                    break
                if command not in directions:
                    print("Use W, A, S, D, R, or Q followed by Enter.")
                    continue
                draw(tick(process, directions[command]))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--runtime", type=Path, required=True)
    parser.add_argument("--scene", type=Path, default=Path(__file__).with_name("scene.json"))
    args = parser.parse_args()
    try:
        play(args.runtime.resolve(), args.scene.resolve())
    except KeyboardInterrupt:
        print("\nGoodbye!")
    except (OSError, RuntimeError, ValueError) as error:
        print(f"Crystal Courier: {error}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
