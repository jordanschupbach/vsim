# Editor protocol v1

Start `vsim_editor_cli`, optionally with `--scene PATH` to preload a scene without
adding an undo entry. `just editor-cli` preloads Crystal Courier by default. The graphical frontend uses the same `Editor` model. Send one
JSON object per line. The optional
`id` is null, a string, or an integer and is echoed in the response. Success is
`{"id":1,"ok":true,"result":...}`; failure is
`{"id":1,"ok":false,"error":"diagnostic"}`. EOF exits normally. Errors do not
terminate the session. Diagnostics must never contaminate stdout.

| Operation | Fields besides `id`, `op` | Result |
|---|---|---|
| `describe` | None | Protocol version, components, commands, Lua API, backend availability |
| `scene` | None | Complete versioned scene |
| `create` | `components` object | New `entity_id` |
| `set` | `entity_id`, `components` | Replace all components; omitted optional components are removed |
| `destroy` | `entity_id` | Delete existing entity |
| `prefab` | `name`, `components` | Define/replace a one-entity prefab |
| `spawn` | `name` | New `entity_id` from a prefab |
| `simulate` | `ticks` integer, 1..10000; optional `actions` object | Execute fresh Lua/Jolt simulation at 60 Hz with the same actions each tick; report ticks/entity count |
| `undo` / `redo` | None | Restore a complete scene snapshot |
| `load` | `path` | Load, validate, and replace scene |
| `save` | `path`, optional boolean `overwrite` (default false) | Atomically publish scene; replacement requires `overwrite:true` |
| `batch` | `commands`: 1..1024 scene mutations | Apply all commands as one undo step; return their results in order |
| `status` | None | Play state, dirty state, current document path, selection, undo/redo availability |
| `select` | `entity_id` or null | Select an existing authored entity or clear selection |
| `play` | None | Create a persistent Lua/Jolt preview from a copy of the authored scene |
| `tick` | `ticks`: 1..10000; optional `actions` | Advance the active preview at 60 Hz |
| `play_scene` | None | Read the current preview scene |
| `stop` | None | Discard the preview; authored scene and history are preserved |
| `view` | None | Editor camera transform and lens, independent of scene camera entities |
| `set_view` | `transform`, `camera` | Validate and replace the editor camera |
| `focus` | Optional `entity_id` (defaults to selection), `aspect` (defaults to 1) | Frame an entity using its scaled bounds, retaining viewing direction |
| `pick` | `screen`: `[u,v]`, optional `aspect` (defaults to 1) | Select the nearest visible mesh triangle; return selection or null |

Unknown fields/operations, duplicate JSON keys, nesting beyond 64 levels, and
malformed values are rejected. Every mutation is
applied to a candidate snapshot and committed only on success. Failed simulation
discards all its scene changes. History retains 32 previous snapshots. New edits
clear redo history. Saving and reading do not modify history.

Each `simulate` request starts fresh Lua states; globals do not carry across
requests. Within one request they persist across ticks. Runtime execution uses
the same lifecycle. Simulation is synchronous; callers should impose process
timeouts appropriate to trusted project size.
Input actions use the contract in `docs/scripting.md`. Malformed actions reject
the command without changing scene/history. For a persistent playable session,
use `play` followed by `tick`, or the runtime's `--interactive` mode.

Play mode rejects authoring, history, selection, loading, and saving. `scene` always
returns the authored document; `play_scene` reads the running copy. A script or
physics failure ends the preview without changing authored data or history.
Malformed tick arguments leave the preview running and do not advance it. `stop`
is idempotent. A new play session starts fresh Lua and physics state.

`batch` permits create/set/destroy/prefab/spawn/simulate only. Nested batches,
file I/O, selection, history, and play commands are rejected. Any failure discards
the whole candidate and preserves undo/redo. Selection clears on load/undo/redo
and when its entity is destroyed. Selection does not create history.

The dirty flag compares authored contents with the last successful load/save (or
initial scene). Failed saves do not change it. Undo/redo can return to the saved
contents. Successful save/load updates the current document path; undo changes
contents while retaining that path.

Camera navigation and picking share these commands with the graphical frontend.
They do not alter scene contents, dirty state, or undo/redo. The editor view starts
from the first scene camera, or an overview camera for empty documents. Loading
resets it; undo/redo retain navigation. `set_view`, `focus`, and `pick` require edit
mode. `view` also works during play and reports the retained authoring camera;
play renders through the game camera. Picking coordinates use a top-left origin,
normalized across the viewport, excluding panels. Out-of-viewport coordinates
clear selection. Invalid requests preserve both view and selection.

IDs are positive integers through `INT64_MAX`, scoped to a scene snapshot.
Re-query IDs after undo/redo/load. Tools using JavaScript must preserve 64-bit
JSON integers rather than rounding them through `Number`.

## Scene schema v1

```json
{
  "version": 1,
  "next_id": 2,
  "entities": [{
    "id": 1,
    "components": {
      "name": "Camera",
      "transform": {
        "position": [0, 2, 5],
        "rotation": [1, 0, 0, 0],
        "scale": [1, 1, 1]
      },
      "camera": {"vertical_fov": 60, "near_plane": 0.1, "far_plane": 1000}
    }
  }],
  "prefabs": {}
}
```

`version` and `entities` are required. `next_id` can be omitted on initial authoring;
the loader chooses one above the highest live ID. Preserve the serialized value
after deletions. Prefabs map names to component objects. Entity IDs must be unique.

Every entity has a name and transform. Omitted names default to `Entity`, transforms
to identity. Optional camera fields have the defaults above. A script component
is `{"source":"function on_update(self,dt) ... end"}`. Sources are embedded so
scene round trips do not depend on external Lua search paths.

Coordinates are right-handed, Y up, meters and seconds; cameras face -Z.
Quaternion order is `[w,x,y,z]`, unit length within 0.0001 squared-norm tolerance.
Scale must be positive; reflection/zero scale is currently unsupported. Camera
FOV is in degrees and strictly between 1 and 179; near must be positive, far > near.
All numeric component values must be finite.

Limits: 10000 entities, 10000 prefab definitions, 128-byte nonempty names without
NUL, 1 MiB per script source, 16 MiB per input line or scene file. Large requests
are consumed and rejected without losing the next protocol line. Scene save uses
a same-filesystem temporary file and an exclusive hard link. By default existing paths are
never overwritten, including under competing writers. Explicit `overwrite:true`
uses same-filesystem atomic replacement (rename on Linux/macOS, MoveFileExW on
Windows); readers observe a complete old or new file. Competing replacement
writers are last-writer-wins. Replacement does not follow destination symlinks. Filesystems without hard
links return an error. Publication is atomic, but power-loss durability (`fsync`)
is not promised. Files are trusted local project files; paths are not sandboxed.

## Renderable and physical entities

The optional `mesh` and `rigid_body` components are independent: a visible object
may have no collider, and an invisible sensor may have no mesh. Both work through
`create`, `set`, `prefab`, `spawn`, undo/redo, and Lua's component JSON API.

```json
{"op":"create","components":{"name":"Ball","transform":{"position":[0,3,0]},"mesh":{"primitive":"sphere","color":[0.8,0.1,0.2],"metallic":0.2,"roughness":0.3},"rigid_body":{"motion":"dynamic","shape":"sphere","mass":2,"restitution":0.7}}}
```

An optional `joint` component on an entity with a dynamic rigid body attaches it
to a parent entity's body (or the world with `parent: 0`) through a hinge or
slider; scene files must reference existing parents other than the entity itself.

```json
{"op":"create","components":{"name":"Arm","transform":{"position":[0,2.4,0],"scale":[0.2,1.2,0.2]},"rigid_body":{"motion":"dynamic","shape":"capsule"},"joint":{"type":"hinge","parent":1,"anchor":[0,0.6,0],"axis":[0,0,1],"limited":true,"limit_min":-1.5,"limit_max":1.5,"damping":0.5}}}
```

Defaults and bounds are in [graphics/physics contracts](graphics-physics.md).
Discovery reports `physics:true`, `physics_backend:"Jolt"`, and `renderer:false`:
the command editor simulates physics and authors meshes but does not own a viewport.
Save a scene path and pass it to `vsim_game --scene PATH` to preview it.
Each editor simulation creates fresh Lua and native physics state from the scene,
then records a single undo transaction. Runtime interactive sessions keep them live.
