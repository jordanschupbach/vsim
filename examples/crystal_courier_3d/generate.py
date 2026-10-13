#!/usr/bin/env python3
"""Author the standalone 3D level from engine components, materials, and Lua."""
import argparse
import json
import math
from pathlib import Path


def generate(root):
    entities = []

    def add(name, position=(0, 0, 0), scale=(1, 1, 1), color=None, primitive="box", body=None, **extra):
        component = {"name": name, "transform": {"position": position, "scale": scale}, **extra}
        if color is not None:
            component["mesh"] = {"primitive": primitive, "color": color, "roughness": 0.55}
        if body is not None:
            component["rigid_body"] = body
        entities.append({"id": len(entities) + 1, "components": component})
        return component

    add("GameController", script={"source": (root / "game.lua").read_text(encoding="utf-8")})
    add("Status: Ready - collect five crystals")
    pitch = -math.atan2(19, 22)
    camera = add("Camera", (0, 19, 22), camera={"vertical_fov": 48, "near_plane": 0.1, "far_plane": 150})
    camera["transform"]["rotation"] = [math.cos(pitch / 2), math.sin(pitch / 2), 0, 0]
    add("Island", (0, -0.65, 0), (20, 1.3, 16), (0.13, 0.22, 0.27), body={})
    # Slightly varied floor tiles provide scale and depth without external assets.
    for x in range(-9, 10, 2):
        for z in range(-7, 8, 2):
            shade = 0.25 if ((x + z) // 2) % 2 else 0.29
            add("Floor tile", (x, 0.018, z), (1.96, 0.03, 1.96), (shade * 0.55, shade * 0.9, shade))
    for x, z, sx, sz in [(-9.7, 0, 0.45, 16), (9.7, 0, 0.45, 16), (0, -7.7, 20, 0.45), (0, 7.7, 20, 0.45)]:
        add("Boundary", (x, 0.45, z), (sx, 0.9, sz), (0.07, 0.13, 0.2), body={})
    # Real static 3D colliders: corridor, low jumping steps, raised collection point.
    for position, scale in [((-2, 0.7, 1), (0.7, 1.4, 7)), ((3, 0.7, -2), (0.7, 1.4, 7)),
                            ((-5, 0.3, -4), (2, 0.6, 2)), ((-7, 0.65, -4), (2, 1.3, 2))]:
        add("Stone", position, scale, (0.34, 0.4, 0.47), body={})
    for index, (x, y, z) in enumerate([(-6.5, 1.0, 5.5), (0, 1.0, -5.5), (6.5, 1.0, -5),
                                        (-7, 2.1, -4), (6.5, 1.0, 2)]):
        add(f"Crystal {index+1}", (x, y, z), (0.65, 1.2, 0.65), (0.06, 0.9, 0.62), "crystal",
            {"sensor": True})["mesh"].update(metallic=0.45, roughness=0.22, emissive=0.35)
        add("Crystal plinth", (x, y - 0.72, z), (0.85, 0.15, 0.85), (0.13, 0.29, 0.3))
    player = add("Player", (-6.5, 1.1, 6.7), (0.9, 0.9, 0.9), (0.87, 0.16, 0.32), "sphere",
                 {"motion": "dynamic", "shape": "sphere", "mass": 2, "friction": 0.1,
                  "restitution": 0, "lock_rotation": True})
    player["mesh"].update(metallic=0.2, roughness=0.28)
    for x, y, z in [(0, 0.55, 3), (1.1, 0.55, 3), (0.55, 1.6, 3), (6, 0.55, -1), (7.1, 0.55, -1)]:
        add("Crate", (x, y, z), (1, 1, 1), (0.55, 0.28, 0.08), body={"motion": "dynamic", "mass": 1.5, "friction": 0.7})
    add("Bouncy ball", (7, 3, 5), (1, 1, 1), (0.15, 0.36, 0.95), "sphere",
        {"motion": "dynamic", "shape": "sphere", "restitution": 0.8, "mass": 0.8})
    add("Lift", (-5, 1.0, 0), (2, 0.35, 2), (0.13, 0.5, 0.65), body={"motion": "kinematic"},
        script={"source": "local t=0; function on_update(self,dt) t=t+dt; engine.set_linear_velocity(self,0,0.8*math.cos(t),0) end"})
    add("Hazard", (5, 0.08, 4.7), (2.8, 0.15, 1.5), (1, 0.025, 0.005), body={"sensor": True})["mesh"]["emissive"] = 0.8
    add("Exit", (7, 0.4, 6.5), (1.8, 0.8, 1.4), (0.8, 0.52, 0.04), body={"sensor": True})["mesh"]["emissive"] = 0.2
    for x in [5.9, 8.1]:
        add("Portal pillar", (x, 1.3, 6.5), (0.3, 2.6, 0.4), (0.8, 0.54, 0.1), body={})
    add("Portal lintel", (7, 2.65, 6.5), (2.5, 0.3, 0.4), (0.8, 0.54, 0.1), body={})
    trophy = {"name": "Trophy", "transform": {"scale": [1.1, 1.8, 1.1]},
              "mesh": {"primitive": "crystal", "color": [1, 0.65, 0.05], "metallic": 0.7, "roughness": 0.2, "emissive": 0.7}}
    return {"version": 1, "next_id": len(entities)+1, "entities": entities, "prefabs": {"Trophy": trophy}}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--check", action="store_true")
    args = parser.parse_args()
    root = Path(__file__).resolve().parent
    text = json.dumps(generate(root), indent=2) + "\n"
    path = root / "scene.json"
    if args.check:
        if path.read_text(encoding="utf-8") != text:
            raise SystemExit("3D Crystal Courier scene is stale; run just generate")
    else:
        path.write_text(text, encoding="utf-8")


if __name__ == "__main__":
    main()
