#!/usr/bin/env python3
"""Exercise actual editor/runtime processes, persistence, and error recovery."""
import json
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest
from concurrent.futures import ThreadPoolExecutor

editor, runtime, source = sys.argv[1:4]
sys.argv = sys.argv[:1]


class ProtocolTests(unittest.TestCase):
    def run_requests(self, requests):
        wire = "\n".join(r if isinstance(r, str) else json.dumps(r) for r in requests) + "\n"
        result = subprocess.run([editor], input=wire, text=True, capture_output=True, timeout=10)
        self.assertEqual(result.returncode, 0, result.stderr)
        responses = [json.loads(line) for line in result.stdout.splitlines()]
        self.assertEqual(len(responses), len(requests))
        return responses

    def test_discovery(self):
        response, = self.run_requests([{"id": "hello", "op": "describe"}])
        self.assertEqual(response["id"], "hello")
        self.assertTrue(response["ok"])
        self.assertIn("spawn", response["result"]["lua_api"])
        self.assertFalse(response["result"]["renderer"])
        self.assertTrue(response["result"]["physics"])
        self.assertEqual(response["result"]["physics_backend"], "Jolt")
        self.assertIn("rigid_body", response["result"]["components"])
        self.assertIn("mesh", response["result"]["components"])
        self.assertIn("apply_impulse", response["result"]["lua_api"])

    def test_physics_edits_simulation_undo_and_failed_edit(self):
        responses = self.run_requests([
            {"op": "create", "components": {"name": "Ball", "mesh": {"primitive": "sphere"},
             "rigid_body": {"motion": "dynamic", "shape": "sphere"},
             "transform": {"position": [0, 4, 0]}}},
            {"op": "scene"}, {"op": "simulate", "ticks": 30}, {"op": "scene"},
            {"op": "undo"}, {"op": "scene"},
            {"op": "set", "entity_id": 1, "components": {"rigid_body": {"mass": 0}}},
            {"op": "scene"}
        ])
        self.assertTrue(all(r["ok"] for r in responses[:6]))
        self.assertLess(responses[3]["result"]["entities"][0]["components"]["transform"]["position"][1], 4)
        self.assertEqual(responses[1]["result"], responses[5]["result"])
        self.assertFalse(responses[6]["ok"])
        self.assertEqual(responses[1]["result"], responses[7]["result"])

    def test_create_set_destroy_undo_redo(self):
        responses = self.run_requests([
            {"op": "create", "components": {"name": "Original"}},
            {"op": "set", "entity_id": 1, "components": {"name": "Edited", "camera": {}}},
            {"op": "scene"}, {"op": "undo"}, {"op": "scene"},
            {"op": "redo"}, {"op": "scene"},
            {"op": "destroy", "entity_id": 1}, {"op": "scene"},
        ])
        self.assertTrue(all(r["ok"] for r in responses))
        self.assertEqual(responses[2]["result"]["entities"][0]["components"]["name"], "Edited")
        self.assertEqual(responses[4]["result"]["entities"][0]["components"]["name"], "Original")
        self.assertIn("camera", responses[6]["result"]["entities"][0]["components"])
        self.assertEqual(responses[8]["result"]["entities"], [])

    def test_invalid_edit_preserves_scene_and_history(self):
        responses = self.run_requests([
            {"op": "create", "components": {"name": "Original"}},
            {"op": "set", "entity_id": 1, "components": {"transform": {"scale": [0, 1, 1]}}},
            {"op": "scene"}, {"op": "undo"}, {"op": "scene"},
        ])
        self.assertFalse(responses[1]["ok"])
        self.assertEqual(responses[2]["result"]["entities"][0]["components"]["name"], "Original")
        self.assertEqual(responses[4]["result"]["entities"], [])

    def test_new_edit_clears_redo(self):
        responses = self.run_requests([
            {"op": "create", "components": {}}, {"op": "undo"},
            {"op": "create", "components": {"name": "New branch"}}, {"op": "redo"},
        ])
        self.assertFalse(responses[-1]["ok"])

    def test_prefab_and_simulation(self):
        responses = self.run_requests([
            {"op": "prefab", "name": "Mover", "components": {"script": {
                "source": "function on_update(self, dt) local x=engine.position(self); engine.set_position(self,x+1,0,0) end"}}},
            {"op": "spawn", "name": "Mover"}, {"op": "simulate", "ticks": 4},
            {"op": "scene"}, {"op": "undo"}, {"op": "scene"},
        ])
        self.assertTrue(all(r["ok"] for r in responses))
        self.assertEqual(responses[3]["result"]["entities"][0]["components"]["transform"]["position"][0], 4)
        self.assertEqual(responses[5]["result"]["entities"][0]["components"]["transform"]["position"][0], 0)

    def test_failed_simulation_is_transactional(self):
        responses = self.run_requests([
            {"op": "create", "components": {"script": {"source":
                "function on_update(self) engine.create('leak'); engine.set_position(self,99,0,0); error('stop') end"}}},
            {"op": "scene"}, {"op": "simulate", "ticks": 1}, {"op": "scene"},
        ])
        self.assertFalse(responses[2]["ok"])
        self.assertIn("script entity 1", responses[2]["error"])
        self.assertEqual(responses[1]["result"], responses[3]["result"])

    def test_malformed_requests_do_not_break_protocol(self):
        requests = ["{invalid", '{"op":"scene","op":"undo"}', '[' * 1000 + '0' + ']' * 1000,
                    [], {"op": "missing"}, {"op": "scene", "typo": 1},
                    {"op": "simulate", "ticks": 1.5}, {"op": "simulate", "ticks": 0},
                    {"op": "simulate", "ticks": 10001}, {"op": "undo"},
                    {"op": "destroy", "entity_id": True}, {"op": "scene"}]
        responses = self.run_requests(requests)
        self.assertTrue(all(not r["ok"] for r in responses[:-1]))
        self.assertTrue(responses[-1]["ok"])

    def test_save_load_and_runtime(self):
        with tempfile.TemporaryDirectory(prefix="vsim-test-") as folder:
            scene = str(Path(folder) / "game.json")
            output = str(Path(folder) / "result.json")
            responses = self.run_requests([
                {"op": "load", "path": str(Path(source) / "scenes/conformance.json")},
                {"op": "save", "path": scene}, {"op": "save", "path": scene},
                {"op": "load", "path": scene}, {"op": "scene"},
            ])
            self.assertTrue(responses[1]["ok"])
            self.assertFalse(responses[2]["ok"])
            result = subprocess.run([runtime, "--scene", scene, "--ticks", "120", "--output", output],
                                    capture_output=True, text=True, timeout=10)
            self.assertEqual(result.returncode, 0, result.stderr)
            exported = json.loads(Path(output).read_text())
            self.assertEqual(exported["entities"][0]["components"]["transform"]["position"][0], 2)
            self.assertEqual(len(exported["entities"]), 3)
            self.assertEqual(sorted(p.name for p in Path(folder).iterdir()), ["game.json", "result.json"])

    def test_failed_load_preserves_current_scene(self):
        with tempfile.TemporaryDirectory() as folder:
            path = Path(folder) / "bad.json"
            path.write_text('{"version":99}')
            responses = self.run_requests([
                {"op": "create", "components": {"name": "Keep me"}},
                {"op": "scene"}, {"op": "load", "path": str(path)}, {"op": "scene"},
            ])
            self.assertFalse(responses[2]["ok"])
            self.assertEqual(responses[1]["result"], responses[3]["result"])

    def test_competing_saves_publish_exactly_one_complete_scene(self):
        with tempfile.TemporaryDirectory() as folder:
            path = Path(folder) / "scene.json"
            def save(name):
                return self.run_requests([
                    {"op": "create", "components": {"name": name}},
                    {"op": "save", "path": str(path)},
                ])[-1]
            with ThreadPoolExecutor(max_workers=2) as workers:
                responses = list(workers.map(save, ["First", "Second"]))
            self.assertEqual(sum(r["ok"] for r in responses), 1)
            contents = json.loads(path.read_text())
            self.assertIn(contents["entities"][0]["components"]["name"], ["First", "Second"])
            self.assertEqual(list(Path(folder).iterdir()), [path])

    def test_oversized_request_recovers_and_oversized_file_is_rejected(self):
        responses = self.run_requests([" " * (16 * 1024 * 1024 + 1), {"op": "scene"}])
        self.assertFalse(responses[0]["ok"])
        self.assertTrue(responses[1]["ok"])
        with tempfile.TemporaryDirectory() as folder:
            path = Path(folder) / "large.json"
            path.write_bytes(b" " * (16 * 1024 * 1024 + 1))
            responses = self.run_requests([{"op": "load", "path": str(path)}, {"op": "scene"}])
            self.assertFalse(responses[0]["ok"])
            self.assertEqual(responses[1]["result"]["entities"], [])

    def test_history_is_bounded(self):
        responses = self.run_requests(
            [{"op": "create", "components": {}} for _ in range(33)] +
            [{"op": "undo"} for _ in range(33)] + [{"op": "scene"}])
        self.assertTrue(all(r["ok"] for r in responses[:65]))
        self.assertFalse(responses[65]["ok"])
        self.assertEqual(len(responses[66]["result"]["entities"]), 1)

    def test_runtime_rejects_invalid_cli(self):
        for arguments in [[], ["--ticks", "-1"], ["--ticks", "2.5"], ["--ticks", "0"],
                          ["--ticks", "100001"], ["--ticks", "1", "--ticks", "1"],
                          ["--no-such-option", "1"], ["--scene"], ["--scene", "missing"]]:
            result = subprocess.run([runtime, *arguments], capture_output=True, text=True, timeout=5)
            self.assertEqual(result.returncode, 1, (arguments, result.stderr))
            self.assertTrue(result.stderr.startswith("vsim_runtime: "), result.stderr)


if __name__ == "__main__":
    unittest.main(verbosity=2)
