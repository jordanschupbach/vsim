# Editor protocol v1

Start `vsim_editor`, optionally with `--scene PATH` to preload a scene without
adding an undo entry. `just editor` preloads Crystal Courier by default. Send one
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
| `save` | `path` | Publish complete scene to a **new** path |

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
use the runtime's `--interactive` mode.

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
a same-filesystem temporary file and an exclusive hard link. Existing paths are
never overwritten, including under competing writers. Filesystems without hard
links return an error. Publication is atomic, but power-loss durability (`fsync`)
is not promised. Files are trusted local project files; paths are not sandboxed.

## Renderable and physical entities

The optional `mesh` and `rigid_body` components are independent: a visible object
may have no collider, and an invisible sensor may have no mesh. Both work through
`create`, `set`, `prefab`, `spawn`, undo/redo, and Lua's component JSON API.

```json
{"op":"create","components":{"name":"Ball","transform":{"position":[0,3,0]},"mesh":{"primitive":"sphere","color":[0.8,0.1,0.2],"metallic":0.2,"roughness":0.3},"rigid_body":{"motion":"dynamic","shape":"sphere","mass":2,"restitution":0.7}}}
```

Defaults and bounds are in [graphics/physics contracts](graphics-physics.md).
Discovery reports `physics:true`, `physics_backend:"Jolt"`, and `renderer:false`:
the command editor simulates physics and authors meshes but does not own a viewport.
Save to a new scene path and pass it to `vsim_game --scene PATH` to preview it.
Each editor simulation creates fresh Lua and native physics state from the scene,
then records a single undo transaction. Runtime interactive sessions keep them live.
