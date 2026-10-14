# Graphical scene editor

Run `just editor` from the Nix development shell to open Crystal Courier 3D.
To edit another scene, use `just editor path/to/scene.json`. To start an empty
scene, build with `just build-editor`, then run `build/debug/vsim_editor`.

The left panel lists entities and offers **Add cube**, **Add entity**, and prefab
controls. Click an entity or a visible mesh to select it. The right panel edits
its name, transform, mesh/material, rigid body, joint, camera, and Lua script. Changes
preview immediately when valid; **Apply** commits them as one undo entry, and
**Revert** restores the authored values. Validation errors remain visible in the
status bar so the fields can be corrected.

Use **Move**, **Rotate**, and **Scale** in the toolbar to choose the gizmo. **Local**
switches its axes to the selected object's orientation. A completed drag creates
one undo entry. The camera controls are RMB + WASD/QE to fly, Shift for faster
movement, the mouse wheel to dolly, and F to focus the selected object. Navigation
does not alter the game's camera component or mark the scene dirty.

**Play** runs a copy of the authored scene with Lua and Jolt. Hover the viewport
and use WASD/Space/R for game input. **Stop** discards the running copy and returns
to editing. A script failure also discards the preview and reports the error.
Editing and saving are disabled during play.

**Save** (Ctrl+S) replaces the current scene file atomically. For an untitled scene,
enter a path and use **Save As**. Existing files require **Replace** to be checked
for Save As. **Open** and window closing prompt before discarding unsaved changes.
Save publication protects complete file contents, but is not a guarantee against
power loss. Prefabs store copies of components; spawned entities are independent.

## Agent automation

The GUI and `just editor-cli` share the validated command model documented in
[the protocol](editor-protocol.md). The GUI's **Commands** panel accepts the same
JSON requests. For reproducible screenshots, a command file schedules requests
before particular rendered frames:

```json
[
  {"frame": 0, "request": {"op": "select", "entity_id": 2}},
  {"frame": 0, "request": {"op": "focus", "aspect": 1.062}},
  {"frame": 2, "request": {"op": "scene"}}
]
```

```sh
just editor path/to/scene.json --offscreen --frames 3 --validate \
  --commands commands.json --capture build/editor.ppm
```

Each scheduled command emits its result as JSON, followed by a final status and
Vulkan validation count. The capture contains the actual rendered scene, panels,
and gizmo. Offscreen mode uses a 1280×800 logical display. Requests must be in
frame order and within the supplied frame count. File-based automation and the
console are available now; there is no remote network service attached to a live
GUI session.

`just test-software` tests actual NVRHI/ImGui drawing, simulated mouse events,
scene creation/picking, gizmo drag and undo, camera navigation, play/stop, file
saving, and GPU teardown. Desktop focus, high-DPI, and platform-specific window
behavior still require host validation. The remaining engine features are listed
in [status](status.md).
