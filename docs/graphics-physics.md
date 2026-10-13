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
| `shape` | `box` | `box`, `sphere` |
| `mass` | 1 | [0.001,1000000] kg; used for dynamic bodies |
| `friction` | 0.6 | [0,1] |
| `restitution` | 0.1 | [0,1] |
| `gravity_scale` | 1 | [0,4]; global gravity is [0,-9.81,0] m/s² |
| `sensor` | false | Boolean; detects overlap without solid response |
| `lock_rotation` | false | Boolean; dynamic translation without angular motion |
| `linear_velocity` | `[0,0,0]` | Each component within +/-500 m/s; static requires zero |

Collider dimensions follow transform scale, constrained to [0.02,1000] m on each
axis. Sphere colliders require uniform scale. Body positions are bounded to
+/-100000 m. Dynamic solids use Jolt continuous collision detection. Static/static
pairs do not produce physical collision response. Shape queries also support
static sensors and sleeping bodies. Jolt owns broadphase, contacts, integration,
friction, restitution, and rigid-body dynamics; the adapter only manages scene
synchronization. Physics is single-threaded. Limits are 10000 scene bodies, 65536
body pairs, and 20000 contact constraints; exceeding Jolt capacity is an error.

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
origin. This is not HDRI/IBL, a configurable lighting system, SSAO, cascaded/contact
shadows, or texture/glTF import. Those remain separate milestones.

Two frames have independent constants, depth/shadow/HDR/output/overlay resources
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
