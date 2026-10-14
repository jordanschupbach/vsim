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
        self.assertIn("set_rotation", response["result"]["lua_api"])

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

    def test_batch_is_one_undo_step_and_failure_preserves_redo(self):
        responses = self.run_requests([
            {"op": "batch", "commands": [
                {"op": "create", "components": {"name": "A"}},
                {"op": "create", "components": {"name": "B"}}]},
            {"op": "undo"}, {"op": "scene"},
            {"op": "batch", "commands": [
                {"op": "create", "components": {"name": "Discard"}},
                {"op": "destroy", "entity_id": 99}]},
            {"op": "redo"}, {"op": "scene"},
        ])
        self.assertTrue(responses[0]["ok"])
        self.assertEqual([r["entity_id"] for r in responses[0]["result"]], [1, 2])
        self.assertEqual(responses[2]["result"]["entities"], [])
        self.assertFalse(responses[3]["ok"])
        self.assertTrue(responses[4]["ok"])
        self.assertEqual([e["components"]["name"] for e in responses[5]["result"]["entities"]], ["A", "B"])

    def test_batch_rejects_nontransactional_commands_and_bad_limits(self):
        requests = [{"op": "batch", "commands": x} for x in [[], {}, [{}] * 1025]]
        requests += [{"op": "batch", "commands": [{"op": op}]} for op in
                     ["batch", "save", "load", "undo", "redo", "select", "play", "tick", "stop"]]
        responses = self.run_requests(requests + [{"op": "status"}])
        self.assertTrue(all(not r["ok"] for r in responses[:-1]))
        self.assertFalse(responses[-1]["result"]["can_undo"])
        self.assertFalse(responses[-1]["result"]["dirty"])

    def test_selection_is_validated_and_cleared_on_snapshot_replacement(self):
        responses = self.run_requests([
            {"op": "create", "components": {}}, {"op": "select", "entity_id": 1},
            {"op": "select", "entity_id": 2}, {"op": "status"},
            {"op": "destroy", "entity_id": 1}, {"op": "status"},
            {"op": "undo"}, {"op": "select", "entity_id": 1},
            {"op": "undo"}, {"op": "status"}, {"op": "select", "entity_id": None},
        ])
        self.assertFalse(responses[2]["ok"])
        self.assertEqual(responses[3]["result"]["selection"], 1)
        self.assertIsNone(responses[5]["result"]["selection"])
        self.assertIsNone(responses[9]["result"]["selection"])
        self.assertTrue(responses[10]["ok"])

    def test_play_preserves_authored_scene_history_and_persistent_lua_state(self):
        responses = self.run_requests([
            {"op": "create", "components": {"script": {"source":
             "local n=0; function on_update(self) n=n+1; engine.set_position(self,n,engine.input('axis'),0) end"}}},
            {"op": "scene"}, {"op": "play"}, {"op": "tick", "ticks": 2},
            {"op": "tick", "ticks": 1, "actions": {"axis": 0.5}},
            {"op": "play_scene"}, {"op": "scene"}, {"op": "stop"},
            {"op": "undo"}, {"op": "scene"}, {"op": "redo"},
            {"op": "play"}, {"op": "tick", "ticks": 1}, {"op": "play_scene"},
        ])
        self.assertTrue(all(r["ok"] for r in responses))
        self.assertEqual(responses[1]["result"], responses[6]["result"])
        self.assertEqual(responses[5]["result"]["entities"][0]["components"]["transform"]["position"], [3, 0.5, 0])
        self.assertEqual(responses[9]["result"]["entities"], [])
        self.assertEqual(responses[13]["result"]["entities"][0]["components"]["transform"]["position"][0], 1)

    def test_play_rejects_editing_and_malformed_ticks_without_advancing(self):
        responses = self.run_requests([
            {"op": "play"}, {"op": "create", "components": {}}, {"op": "undo"},
            {"op": "save", "path": "unused"}, {"op": "tick", "ticks": 0},
            {"op": "tick", "ticks": 1, "actions": {"axis": 2}},
            {"op": "status"}, {"op": "play_scene"}, {"op": "stop"},
            {"op": "tick", "ticks": 1}, {"op": "play_scene"},
        ])
        self.assertTrue(all(not r["ok"] for r in responses[1:6]))
        self.assertTrue(responses[6]["result"]["playing"])
        self.assertFalse(responses[6]["result"]["dirty"])
        self.assertEqual(responses[7]["result"]["entities"], [])
        self.assertFalse(responses[9]["ok"])
        self.assertFalse(responses[10]["ok"])

    def test_play_failure_stops_preview_and_preserves_authoring(self):
        responses = self.run_requests([
            {"op": "create", "components": {"script": {"source":
             "function on_update(self) engine.create('discard'); error('preview failed') end"}}},
            {"op": "scene"}, {"op": "play"}, {"op": "tick", "ticks": 1},
            {"op": "status"}, {"op": "scene"}, {"op": "undo"}, {"op": "scene"},
        ])
        self.assertFalse(responses[3]["ok"])
        self.assertIn("preview failed", responses[3]["error"])
        self.assertFalse(responses[4]["result"]["playing"])
        self.assertEqual(responses[1]["result"], responses[5]["result"])
        self.assertEqual(responses[7]["result"]["entities"], [])

    def test_save_replacement_and_dirty_state_follow_saved_contents(self):
        with tempfile.TemporaryDirectory() as folder:
            path = str(Path(folder) / "game.json")
            responses = self.run_requests([
                {"op": "create", "components": {"name": "Original"}},
                {"op": "save", "path": path}, {"op": "status"},
                {"op": "set", "entity_id": 1, "components": {"name": "Edited"}},
                {"op": "status"}, {"op": "save", "path": path}, {"op": "status"},
                {"op": "save", "path": path, "overwrite": True}, {"op": "status"},
                {"op": "undo"}, {"op": "status"}, {"op": "redo"}, {"op": "status"},
            ])
            self.assertFalse(responses[2]["result"]["dirty"])
            self.assertTrue(responses[4]["result"]["dirty"])
            self.assertFalse(responses[5]["ok"])
            self.assertTrue(responses[6]["result"]["dirty"])
            self.assertTrue(responses[7]["ok"])
            self.assertFalse(responses[8]["result"]["dirty"])
            self.assertEqual(responses[8]["result"]["path"], path)
            self.assertTrue(responses[10]["result"]["dirty"])
            self.assertFalse(responses[12]["result"]["dirty"])
            self.assertEqual(json.loads(Path(path).read_text())["entities"][0]["components"]["name"], "Edited")
            self.assertEqual(list(Path(folder).iterdir()), [Path(path)])

    def test_failed_replacement_preserves_destination_and_history(self):
        with tempfile.TemporaryDirectory() as folder:
            path = Path(folder) / "directory"
            path.mkdir()
            responses = self.run_requests([
                {"op": "create", "components": {}},
                {"op": "save", "path": str(path), "overwrite": True},
                {"op": "status"}, {"op": "undo"}, {"op": "scene"},
            ])
            self.assertFalse(responses[1]["ok"])
            self.assertTrue(responses[2]["result"]["dirty"])
            self.assertEqual(responses[2]["result"]["path"], "")
            self.assertEqual(responses[4]["result"]["entities"], [])
            self.assertTrue(path.is_dir())
            self.assertEqual(list(Path(folder).iterdir()), [path])

    def test_explicit_replacement_does_not_write_through_symlinks(self):
        with tempfile.TemporaryDirectory() as folder:
            target = Path(folder) / "target.txt"
            target.write_text("keep this file")
            path = Path(folder) / "scene.json"
            path.symlink_to(target)
            responses = self.run_requests([
                {"op": "create", "components": {"name": "Replacement"}},
                {"op": "save", "path": str(path), "overwrite": True},
            ])
            self.assertTrue(responses[1]["ok"])
            self.assertFalse(path.is_symlink())
            self.assertEqual(target.read_text(), "keep this file")
            self.assertEqual(json.loads(path.read_text())["entities"][0]["components"]["name"], "Replacement")

    def test_view_commands_and_picking_are_shared_without_scene_mutation(self):
        view = {"transform": {"position": [0, 0, 5]}, "camera": {"far_plane": 100}}
        responses = self.run_requests([
            {"op": "create", "components": {"mesh": {"primitive": "box"}}},
            {"op": "scene"}, {"op": "set_view", **view},
            {"op": "pick", "screen": [0.5, 0.5]}, {"op": "status"},
            {"op": "focus", "aspect": 0.5}, {"op": "view"}, {"op": "scene"},
            {"op": "undo"}, {"op": "scene"}, {"op": "view"},
        ])
        self.assertTrue(all(r["ok"] for r in responses))
        self.assertEqual(responses[3]["result"]["selection"], 1)
        self.assertEqual(responses[4]["result"]["selection"], 1)
        self.assertEqual(responses[1]["result"], responses[7]["result"])
        self.assertEqual(responses[9]["result"]["entities"], [])
        self.assertEqual(responses[6]["result"], responses[10]["result"])
        self.assertGreater(responses[6]["result"]["transform"]["position"][2], 3)

    def test_invalid_navigation_preserves_view_selection_and_history(self):
        initial = {"transform": {"position": [0, 0, 5]}, "camera": {}}
        invalid = [
            {"op": "set_view", "transform": {"scale": [0, 1, 1]}, "camera": {}},
            {"op": "set_view", "transform": {}, "camera": {"near_plane": -1}},
            {"op": "set_view", "transform": {}, "camera": None},
            {"op": "focus", "entity_id": 99},
            {"op": "focus", "aspect": 0}, {"op": "focus", "aspect": True},
            {"op": "pick", "screen": [0.5]}, {"op": "pick", "screen": [True, 0.5]},
            {"op": "pick", "screen": [1e300, 0.5]},
            {"op": "pick", "screen": [0.5, 0.5], "aspect": -1},
        ]
        responses = self.run_requests([
            {"op": "create", "components": {"mesh": {}}},
            {"op": "set_view", **initial}, {"op": "view"},
            {"op": "select", "entity_id": 1}, *invalid,
            {"op": "view"}, {"op": "status"},
            {"op": "pick", "screen": [-1, 0]}, {"op": "status"},
            {"op": "undo"}, {"op": "scene"},
        ])
        self.assertTrue(all(not r["ok"] for r in responses[4:4 + len(invalid)]))
        self.assertEqual(responses[2]["result"], responses[-6]["result"])
        self.assertEqual(responses[-5]["result"]["selection"], 1)
        self.assertIsNone(responses[-3]["result"]["selection"])
        self.assertEqual(responses[-1]["result"]["entities"], [])

    def test_view_initializes_from_scene_camera_and_resets_on_load(self):
        with tempfile.TemporaryDirectory() as folder:
            path = Path(folder) / "camera.json"
            path.write_text(json.dumps({"version": 1, "entities": [
                {"id": 1, "components": {"camera": {"vertical_fov": 75},
                                          "transform": {"position": [2, 3, 4]}}}]}))
            responses = self.run_requests([
                {"op": "load", "path": str(path)}, {"op": "view"},
                {"op": "set_view", "transform": {}, "camera": {}},
                {"op": "status"}, {"op": "load", "path": str(path)}, {"op": "view"},
                {"op": "play"}, {"op": "view"},
                {"op": "set_view", "transform": {}, "camera": {}},
                {"op": "focus", "entity_id": 1}, {"op": "pick", "screen": [0.5, 0.5]},
            ])
            self.assertFalse(responses[3]["result"]["dirty"])
            self.assertEqual(responses[1]["result"], responses[5]["result"])
            self.assertEqual(responses[1]["result"]["camera"]["vertical_fov"], 75)
            self.assertEqual(responses[1]["result"]["transform"]["position"], [2, 3, 4])
            self.assertEqual(responses[1]["result"], responses[7]["result"])
            self.assertTrue(all(not r["ok"] for r in responses[8:]))

    def test_runtime_rejects_invalid_cli(self):
        for arguments in [[], ["--ticks", "-1"], ["--ticks", "2.5"], ["--ticks", "0"],
                          ["--ticks", "100001"], ["--ticks", "1", "--ticks", "1"],
                          ["--no-such-option", "1"], ["--scene"], ["--scene", "missing"]]:
            result = subprocess.run([runtime, *arguments], capture_output=True, text=True, timeout=5)
            self.assertEqual(result.returncode, 1, (arguments, result.stderr))
            self.assertTrue(result.stderr.startswith("vsim_runtime: "), result.stderr)


if __name__ == "__main__":
    unittest.main(verbosity=2)
