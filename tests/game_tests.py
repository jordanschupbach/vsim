#!/usr/bin/env python3
"""Integration tests against the real Lua game and persistent C++ runtime."""
import json
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

runtime = Path(sys.argv[1]).resolve()
root = Path(sys.argv[2]).resolve()
sys.argv = sys.argv[:1]
scene = root / "examples/crystal_courier/scene.json"
directions = {"w": {"move_z": -1}, "s": {"move_z": 1},
              "a": {"move_x": -1}, "d": {"move_x": 1}}


def components(snapshot, name):
    return [e["components"] for e in snapshot["entities"] if e["components"]["name"] == name]


def status(snapshot):
    return next(e["components"]["name"] for e in snapshot["entities"]
                if e["components"]["name"].startswith("Status: "))


class GameTests(unittest.TestCase):
    def requests(self, requests, path=scene, code=0):
        wire = "\n".join(r if isinstance(r, str) else json.dumps(r) for r in requests) + "\n"
        result = subprocess.run([str(runtime), "--scene", str(path), "--interactive"],
                                input=wire, text=True, capture_output=True, timeout=10)
        self.assertEqual(result.returncode, code, result.stderr)
        responses = [json.loads(line) for line in result.stdout.splitlines()]
        return responses

    def moves(self, moves):
        requests = [{"op": "tick"}] + [{"op": "tick", "actions": directions[c]} for c in moves]
        responses = self.requests(requests)
        self.assertEqual(len(responses), len(requests))
        self.assertTrue(all(r["ok"] for r in responses))
        return responses[-1]["result"]

    def test_complete_game_and_victory_is_stable(self):
        snapshot = self.moves("ddddsssaaaddddwdwwdddd")
        self.assertIn("Victory! Delivered all 3 crystals in 18 moves.", status(snapshot))
        self.assertEqual(components(snapshot, "Crystal"), [])
        self.assertEqual(len(components(snapshot, "Trophy")), 1)
        self.assertEqual(components(snapshot, "Player")[0]["transform"]["position"], [7, 0, 1])

    def test_wall_blocks_movement(self):
        snapshot = self.moves("wa")
        self.assertEqual(components(snapshot, "Player")[0]["transform"]["position"], [1, 0, 1])
        self.assertIn("wall blocks", status(snapshot))
        self.assertEqual(len(components(snapshot, "Crystal")), 3)

    def test_trap_loses_and_stays_terminal(self):
        snapshot = self.moves("ssssdddd")
        self.assertIn("Game over!", status(snapshot))
        self.assertEqual(components(snapshot, "Player"), [])
        self.assertEqual(components(snapshot, "Trophy"), [])

    def test_exit_requires_every_crystal(self):
        snapshot = self.moves("dddddd")
        self.assertIn("Exit locked: 2 crystals remain.", status(snapshot))
        self.assertEqual(components(snapshot, "Trophy"), [])

    def test_bad_input_and_editor_commands_are_rejected_without_mutation(self):
        requests = [{"op": "tick"}, {"op": "tick", "actions": {"move_x": True}},
                    {"op": "create", "components": {}}, "{bad", {"op": "scene"},
                    {"id": "move", "op": "tick", "actions": {"move_x": 1}}, {"op": "tick"}]
        responses = self.requests(requests)
        self.assertTrue(all(not r["ok"] and not r["fatal"] for r in responses[1:4]))
        self.assertEqual(responses[0]["result"], responses[4]["result"])
        self.assertEqual(responses[5]["id"], "move")
        self.assertEqual(responses[5]["result"], responses[6]["result"])

    def test_script_error_is_fatal(self):
        with tempfile.TemporaryDirectory() as folder:
            path = Path(folder) / "failure.json"
            path.write_text(json.dumps({"version": 1, "entities": [{"id": 1, "components": {
                "script": {"source": "function on_update() error('game failure') end"}}}]}))
            responses = self.requests([{"op": "tick"}, {"op": "scene"}], path=path, code=1)
            self.assertEqual(len(responses), 1)
            self.assertFalse(responses[0]["ok"])
            self.assertTrue(responses[0]["fatal"])
            self.assertIn("game failure", responses[0]["error"])

    def test_interactive_cli_rejects_batch_options(self):
        for option in [["--ticks", "1"], ["--output", "unused.json"], ["--output", ""], ["--interactive"]]:
            result = subprocess.run([str(runtime), "--scene", str(scene), "--interactive", *option],
                                    input="", text=True, capture_output=True, timeout=5)
            self.assertEqual(result.returncode, 1)
            self.assertTrue(result.stderr.startswith("vsim_runtime: "))

    def test_terminal_frontend_win_restart_and_quit(self):
        commands = "\n".join("ddddsssaaaddddwdww") + "\nr\nq\n"
        result = subprocess.run([sys.executable, str(root / "examples/crystal_courier/play.py"),
                                 "--runtime", str(runtime)], input=commands, text=True,
                                capture_output=True, timeout=10)
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn("Victory!", result.stdout)
        self.assertEqual(result.stdout.count("Collect all 3 crystals"), 2)

    def test_terminal_eof_exits_cleanly(self):
        result = subprocess.run([sys.executable, str(root / "examples/crystal_courier/play.py"),
                                 "--runtime", str(runtime)], input="", text=True,
                                capture_output=True, timeout=5)
        self.assertEqual(result.returncode, 0, result.stderr)

    def test_terminal_rejects_unrenderable_edited_coordinates(self):
        for coordinate in [-1, 0.5, 1e30]:
            with tempfile.TemporaryDirectory() as folder:
                path = Path(folder) / "edited.json"
                document = json.loads(scene.read_text())
                player = next(e["components"] for e in document["entities"] if e["components"]["name"] == "Player")
                player["transform"]["position"][0] = coordinate
                path.write_text(json.dumps(document))
                result = subprocess.run([sys.executable, str(root / "examples/crystal_courier/play.py"),
                                         "--runtime", str(runtime), "--scene", str(path)],
                                        input="", text=True, capture_output=True, timeout=5)
                self.assertEqual(result.returncode, 1)
                self.assertIn("integer X/Z coordinates", result.stderr)
                self.assertNotIn("Traceback", result.stderr)


if __name__ == "__main__":
    unittest.main(verbosity=2)
