# Graphics and physics contracts

## Scene components

All numbers must be finite. Omitted fields use these defaults; unknown fields and
wrong types are rejected. Scene version 1 retains compatibility with old scenes
that do not contain the new optional components.

| Mesh field | Default | Accepted values |
|---|---|---|
| `primitive` | `box` | `box`, `sphere`, `crystal` |
| `color` | `[0.6,0.6,0.6]` | Linear RGB, each in [0,1] |
| `metallic` | 0 | [0,1] |
| `roughness` | 0.6 | [0.04,1] |
| `emissive` | 0 | [0,10], multiplier of base color |

Meshes have unit dimensions centered at the origin. Transform scale determines
world dimensions. Crystal geometry is an octahedron; its example sensor uses a
box collider. Sphere normals are smooth; box/crystal faces have flat normals.

| Rigid-body field | Default | Accepted values |
|---|---|---|
| `motion` | `static` | `static`, `dynamic`, `kinematic` |
| `shape` | `box` | `box`, `sphere`, `capsule` |
| `mass` | 1 | [0.001,1000000] kg; used for dynamic bodies |
| `friction` | 0.6 | [0,1] |
| `restitution` | 0.1 | [0,1] |
| `gravity_scale` | 1 | [0,4]; global gravity is [0,-9.81,0] m/s² |
| `sensor` | false | Boolean; detects overlap without solid response |
| `lock_rotation` | false | Boolean; shorthand for `allowed_rotation` all false |
| `allowed_translation` | `[true,true,true]` | World axes a dynamic body may move along |
| `allowed_rotation` | `[true,true,true]` | World axes a dynamic body may turn about |
| `linear_velocity` | `[0,0,0]` | Each component within +/-500 m/s; static requires zero |
| `angular_velocity` | `[0,0,0]` | World space, each component within +/-100 rad/s; static requires zero |

Collider dimensions follow transform scale, constrained to [0.02,1000] m on each
axis. Sphere colliders require uniform scale. Capsules run along the local Y axis:
the x and z scales are the diameter (they must match) and the y scale is the full
tip-to-tip length, which must exceed the diameter. A dynamic body needs at least
one allowed degree of freedom; `[true,true,false]` translation with `[false,false,true]`
rotation gives a planar (XY) body. Body positions are bounded to
+/-100000 m. Dynamic solids use Jolt continuous collision detection. Static/static
pairs do not produce physical collision response. Shape queries also support
static sensors and sleeping bodies. Jolt owns broadphase, contacts, integration,
friction, restitution, and rigid-body dynamics; the adapter only manages scene
synchronization. Physics is single-threaded. Limits are 10000 scene bodies, 65536
body pairs, and 20000 contact constraints; exceeding Jolt capacity is an error.
Jolt caps speeds at 500 m/s and 100 rad/s and applies its default 0.05 linear and
angular damping. There is no capsule mesh primitive yet; visualize a capsule body
with a scaled sphere or box mesh.

## Joints

An optional `joint` component connects the entity's own dynamic rigid body to a
parent entity's rigid body (static, kinematic, or dynamic), or to the world when
`parent` is 0. Joints are Jolt two-body constraints. Bodies connected by a joint
never collide with each other; everything else collides normally.

| Joint field | Default | Accepted values |
|---|---|---|
| `type` | `hinge` | `hinge` (one rotation, radians) or `slider` (one translation, meters) |
| `parent` | 0 | Entity ID with a rigid body, or 0 for the world; never the entity itself |
| `anchor` | `[0,0,0]` | Pivot in the entity's rotated local frame, meters, scale ignored; within +/-1000 |
| `axis` | `[0,0,1]` | Hinge or sliding direction in the same frame; nonzero, normalized by the engine |
| `limited` | false | Boolean |
| `limit_min` | 0 | `limit_min <= 0 <= limit_max`; hinges within [-pi, pi], sliders within +/-1000 m |
| `limit_max` | 0 | As above |
| `damping` | 0 | [0,1000000]; viscous torque (N m s/rad) or force (N s/m) opposing joint velocity |

The joint coordinate is zero at the pose from which the native constraint was
built: the authored transforms must already place parent and child in a valid
joint configuration, and rebuilding the world (loading a scene, editor undo or
simulate) rezeroes the coordinate at the current pose. A positive hinge angle is a
right-handed rotation of the child about `axis` relative to the parent; a
positive slider position moves the child along `axis`. Joint parents are
validated when the physics world synchronizes: a missing parent, a parent without
a rigid body, or a cycle of joints is an error that leaves the scene unchanged.
Destroying a parent or removing its rigid body fails the next synchronization.
Changing any joint field, or rebuilding either body, rebuilds the constraint.

`PhysicsWorld::joint_state`, `set_joint_state`, and `apply_joint_force` (and the
matching Lua bindings) read, teleport, and actuate joints; see
[scripting](scripting.md). Setting a joint moves every body jointed below it as a
rigid subtree and keeps the other joint coordinates. Joint forces accumulate
until the next step and are then cleared, like Jolt forces. The adapter applies
`damping` itself before each step. Limits are hard; a fast body overshoots a
limit by at most one step before the solver returns it.

`Simulation` owns a physics world and Lua instances. Scene references must outlive
it and must not be replaced underneath it. See [Lua lifecycle](scripting.md).

## Rendering

`vsim_renderer` is a static adapter around GLFW, Vulkan 1.3, and NVRHI. It requires
timeline semaphores, synchronization2, maintenance4, and dynamic rendering.
Window surfaces must support BGRA8 sRGB and transfer destinations. Unsupported
features fail with diagnostics; offscreen rendering is an explicit option.

The first camera by entity ID supplies a perspective view. Coordinates are
right-handed, Y up, with cameras looking down local -Z. Camera projection uses
Vulkan zero-to-one depth; the renderer adapts its Y convention to NVRHI's
negative-height viewport. Depth, winding, shadow orientation, and HUD placement
have GPU readback tests.

The forward renderer uses metallic/roughness GGX shading, a fixed directional
sun, a 2048² depth shadow map with 3x3 PCF, hemispheric ambient light, RGBA16F HDR,
and an ACES-fitted tonemap into sRGB. Shadows cover a fixed 36 m region about the
origin. A full-resolution SSAO pass samples 32 directions in the surface's world-space
hemisphere, followed by horizontal and vertical seven-tap depth/normal-aware filters.
Indirect light has a separate HDR attachment; AO attenuates that term before
tonemapping, leaving the direct-light and emission terms intact. Position
reconstruction accounts for the editor's subviewport and NVRHI's Y convention.

`RendererConfig::ambient_occlusion` and `Renderer::set_ambient_occlusion` accept
`enabled` (default true), world-space `radius` (default 1 m, range 0.01..100),
`bias` (default 0.035 m, range 0 <= bias < radius), `strength` (default 1.5, range
0..4), and `visualize` (default false). Disabled AO and zero strength produce unit
visibility. All values are validated before changing renderer state. The game
accepts `--no-ssao` and `--show-ssao` for comparison and diagnostic captures.

SSAO uses only the current frame's visible surfaces; hidden/offscreen occluders
and temporal reprojection are not represented. HDRI/IBL, configurable lighting,
cascaded/contact shadows, and texture/glTF import remain separate milestones.

Two frames have independent constants, depth/shadow/HDR/ambient/normal/AO/output/overlay resources
and completion queries. Swapchain presentation semaphores belong to swapchain
images. Resize waits for idle before releasing targets; minimized windows skip
rendering and wait briefly for events. Physics continues while minimized. Device
loss terminates with an error rather than attempting recovery. NVRHI resources
are released before the Vulkan device and surface. The static Vulkan-Hpp
configuration allows only one live `Renderer`; overlapping creation is rejected.
All GLFW/Renderer calls belong on the main thread.

Captures wait for GPU completion, honor staging row pitch, convert BGRA to RGBA,
and write binary PPM. Screenshot paths may be overwritten; scene `--output` paths
retain the exclusive, non-overwriting scene-save contract. Dimensions are bounded
to 64..4096 for explicit configuration/resize. Shaders compile with `glslc` and
are embedded in the executable; no working-directory shader lookup is needed.

## Dependencies and notices

`dependencies.json` pins NVRHI, Jolt 5.3.0, and Vulkan-Headers by full Git commit.
`just deps` fetches them explicitly into ignored `.cache/dependencies` and refuses
modified checkouts. Nix uses fixed-output GitHub archives from the same revisions;
`nix_hash` values were computed from the corresponding `git archive` source trees.
A full Nix build is still required to validate remote archive fetching.

Backend license notices and the manifest are installed with the runtimes. Source
locations are [NVRHI](https://github.com/NVIDIA-RTX/NVRHI),
[Jolt](https://github.com/jrouwe/JoltPhysics), and
[Vulkan-Headers](https://github.com/KhronosGroup/Vulkan-Headers).
Other system libraries retain their upstream licenses in the Nix closure. Portable
export must gather all dependency notices and runtime files into the game package.
