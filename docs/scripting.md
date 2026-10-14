# Lua 5.4 contract

Each entity with a script component owns an independent Lua state. Source executes
once, then optional `on_start(self, dt)` executes once, then optional
`on_update(self, dt)` executes every tick, including the initial tick. `self` is
the entity's integer ID. Runtime and editor simulation use `dt = 1/60` seconds.
Local and global Lua variables persist across ticks of that script instance.

Entities are visited in ascending ID order. The tick snapshots IDs before running
scripts. An entity spawned during a callback first runs on the next tick. Destroying
an entity prevents its later callback in the same tick. Destroying self during
`on_start` skips `on_update`. Changing source rebuilds that instance on its next
visit. Removing a script removes its instance at the end of the tick.

Script errors identify the entity and propagate to the caller. The runtime aborts
on failure; it does not promise rollback of earlier successful callbacks. Editor
`simulate` runs on a temporary scene and rolls back the entire command on failure.
There is no `on_destroy` callback yet.
Lua finalizers cannot call the engine API during instance shutdown or reenter it
while another engine API call is producing return values.

| Function | Contract |
|---|---|
| `engine.create(name)` | Create default entity and return ID |
| `engine.destroy(id)` | Destroy an existing entity; stale IDs error |
| `engine.alive(id)` | Return whether a positive integer ID is live |
| `engine.entities()` | Return an ordered snapshot array of IDs |
| `engine.get(id)` | Return components as a JSON string |
| `engine.name(id)` | Return the entity's name; stale IDs error |
| `engine.input(action)` | Return this tick's normalized action value; missing actions return zero |
| `engine.set(id, json)` | Validate and replace components from JSON string |
| `engine.position(id)` | Return x, y, z |
| `engine.set_position(id, x, y, z)` | Change position; retain other components |
| `engine.set_rotation(id, w, x, y, z)` | Set a finite unit quaternion in w/x/y/z order; retain other components; invalid rotations leave the entity unchanged |
| `engine.linear_velocity(id)` | Read rigid-body velocity as x, y, z in m/s |
| `engine.set_linear_velocity(id, x, y, z)` | Set a dynamic/kinematic body's velocity; static bodies reject it |
| `engine.angular_velocity(id)` | Read rigid-body angular velocity as x, y, z in rad/s (world space) |
| `engine.set_angular_velocity(id, x, y, z)` | Set a dynamic/kinematic body's angular velocity; static bodies reject it |
| `engine.apply_impulse(id, x, y, z)` | Apply a world-space linear impulse in N s to a dynamic body |
| `engine.touching(a, b)` | Test shape overlap/proximity within 0.02 m, including sensors and sleeping bodies |
| `engine.grounded(id)` | Downward center probe to 0.08 m below the body's bounds; solid surface normal Y must exceed 0.6 |
| `engine.joint_state(id)` | Return the joint coordinate (rad or m) and its rate (rad/s or m/s) of the entity's joint |
| `engine.set_joint_state(id, position, rate)` | Teleport the joint and the bodies below it to a coordinate within its limits and set its rate |
| `engine.apply_joint_force(id, value)` | Apply a torque (N m, hinge) or force (N, slider) along the joint axis for the next step; finite, within +/-100000 |
| `engine.define_prefab(name, json)` | Define/replace a component template |
| `engine.spawn(name)` | Create independent copy of a prefab and return ID |

```lua
local elapsed = 0
function on_update(self, dt)
    elapsed = elapsed + dt
    engine.set_position(self, math.sin(elapsed), 0, 0)
end
```

## Input frames and interactive runtime

`ScriptSystem::tick(delta, input)` accepts up to 32 named actions. Names are valid
UTF-8, 1..64 bytes without NUL; values are finite numbers in `[-1,1]`. All scripts
observe the same copied input frame, including source initialization and
`on_start`. A missing frame clears every action to zero; values do not latch.
Invalid frames fail before any callback runs. Each game defines its action names.

Run `vsim_runtime --scene PATH --interactive` to keep one scene and Simulation
alive across newline-delimited JSON requests. `--interactive` cannot be combined
with batch `--ticks` or `--output` options. The supported requests are:

```json
{"id":1,"op":"scene"}
{"id":2,"op":"tick","actions":{"move_x":1,"move_z":0}}
{"id":3,"op":"tick"}
```

`scene` inspects state without running scripts. Each `tick` advances exactly
1/60 second; it initializes scripts on the first tick and preserves their locals
and globals thereafter. Responses echo `id` and contain `ok` and the complete
scene in `result`. This transport is intended for small headless games/testing;
it sends scene snapshots, not a high-throughput render stream.

Malformed requests return `ok:false`, an `error` diagnostic, and `fatal:false`;
the caller may send another request. Script execution failures return `fatal:true`
and terminate with exit code 1, because earlier callbacks may have changed state.
EOF exits normally. Editor mutation commands are unavailable in the runtime.
Crystal Courier's terminal client uses this protocol; gameplay stays in Lua.

The base, table, string, math, and UTF-8 libraries are available. File/process,
package loading, debugging, coroutine, printing, dynamic loading, `pcall`, and
`xpcall` are unavailable. Source and each callback receive a 100000-instruction
budget, checked every 1000 VM instructions. This catches ordinary infinite Lua
loops; it does not bound work or memory inside native library functions.

**Scripts are trusted project code, not a security boundary for untrusted mods.**
Memory quotas, process isolation, wall-clock preemption, and deterministic RNG
are not implemented. Do not advertise hostile-script containment or bit-identical
cross-machine replays. The engine is single-threaded and its scene references
must not be replaced while a ScriptSystem uses them.

`scenes/conformance.lua` wraps and checks every registered engine function. Add
an exercise there when adding a binding; regenerate the embedded scene with
`python3 scripts/generate_conformance.py`. Tests verify scene source synchronization.

## Physics lifecycle

`Simulation::tick` validates input and delta, reconciles authored bodies, runs Lua,
then steps Jolt and copies body transforms/velocities back to the scene. Use this
entry point for gameplay; a standalone `ScriptSystem` has no physics backend.
The runtime, graphical game, and editor all use `Simulation`. Deltas must be in
`(0, 1/30]` seconds; the executables use 1/60. Queries and impulses reconcile scene
edits immediately, including spawned/destroyed bodies. Queries observe the state
before the current tick's final physics step.

All physics APIs require a valid entity with a rigid body, and the joint APIs an
entity with a joint. Impulses require a dynamic body, finite components within
+/-100000 N s, and a resulting speed no greater than 500 m/s. Linear velocity
components must be within +/-500 m/s and angular ones within +/-100 rad/s; Jolt
clamps integrated speeds to the same bounds. Invalid values raise a script error.
Joint coordinates are measured from the authored pose; see the joint contract in
[graphics/physics contracts](graphics-physics.md). `set_joint_state` validates the
new poses of the whole subtree before moving anything. `apply_joint_force` acts
on the child and, when the parent is dynamic, oppositely on the parent.
`grounded` is a center ray probe suitable for this sphere-controlled example,
not a general character controller or a collision-event callback. Sensors do
not count as ground. Contact queries support sleeping bodies.

Changing collision shape, scale, or body configuration rebuilds the native body
and the joints attached to it; position/rotation edits teleport it. Transform,
linear velocity, and angular velocity are serialized. Native sleep state,
contacts, accumulated constraint impulses, joint zero poses, and Lua locals are
not save-game data. Editor simulation/undo reconstructs native worlds from scene
snapshots. See [graphics/physics contracts](graphics-physics.md).
