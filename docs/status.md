# Implementation status and release gates

VSim now runs a real 3D example on Linux. This does not certify the full engine
from the original product brief. See the actual results in [validation](validation.md).

| Area | Current evidence / required work |
|---|---|
| C++ static core, CMake, separate editor/runtimes | Implemented and covered by installation/protocol tests |
| Entities, camera, mesh, rigid body, strict scenes | Validated components, round trips, prefab and undo tests |
| Lua lifecycle and physics API | 17 bindings; conformance scene covers every binding |
| Crystal Courier 3D | Perspective arena, jumping, crates, bouncing ball, moving lift, sensors, victory/restart; input-only winning route tested |
| GLFW/Vulkan/NVRHI | Offscreen rendering/readback/resize/device lifetime tested on Mesa llvmpipe; window presentation implemented but local desktop access blocked |
| PBR/HDR/shadows | Procedural meshes, metallic/roughness GGX, RGBA16F, ACES-fitted tonemap, directional PCF shadow map |
| glTF/textures, HDRI/IBL, SSAO | Not implemented |
| Jolt physics | Real static/dynamic/kinematic bodies, box/sphere, CCD, sensors, ground queries, impulses, Lua control |
| Agent editing | Transactional JSON commands, components, simulation, scene save/preview; graphical editor/gizmos not implemented |
| Audio | miniaudio not yet integrated |
| Export | Both runtimes/scenes/notices install without editor; portable packaging not implemented |
| Windows / macOS | Not built or tested; macOS NVRHI compatibility unresolved |
| Git / GitHub | Workspace `.git` protected; shell cannot resolve GitHub |

## Remaining release gates

1. Run window presentation/resizing with `just test-window`, and manually test
   minimize/restore, focus changes, controls, and high-DPI behavior. Add physical
   AMD/Intel/NVIDIA driver coverage. Software Vulkan image checks are not hardware
   certification or a performance claim.
2. Run leak checking outside this restricted process-inspection environment,
   `nix flake check`, and CI. Validate the fixed-output archive downloads against
   the checked-in source hashes. Review the complete diff before commit/push;
   inspect remote history and never force-push.
3. Add bounded glTF/textures import, HDR environment preprocessing/IBL, SSAO,
   configurable lighting, and larger-world shadows. Extend visual scenes/tests
   for each feature. Poly Haven assets need cached provenance and package notices.
4. Add character-controller/contact-event APIs, robust physics state checkpoints,
   and miniaudio with device-loss/headless tests. Current shape queries and scene
   snapshots are not full collision event streams or save-game state.
5. Add graphical scene hierarchy/inspector, transform gizmos, play/stop snapshots,
   and editor rendering. UI and agents should invoke the same transactional commands.
6. Implement asset cooking and portable packages with full dependency notices,
   validate on clean Linux machines, then add Windows and a macOS Vulkan/MoltenVK
   compatibility spike before promising those platforms.

Upstream references: [NVRHI](https://github.com/NVIDIA-RTX/NVRHI),
[GLFW](https://github.com/glfw/glfw), [GLM](https://github.com/g-truc/glm),
[Lua](https://www.lua.org/manual/5.4/manual.html),
[Jolt](https://github.com/jrouwe/JoltPhysics),
[miniaudio](https://github.com/mackron/miniaudio), and
[Poly Haven](https://polyhaven.com/hdris). Exact integrated source revisions and
licenses are in `dependencies.json`; practical limits are in
[graphics/physics contracts](graphics-physics.md).
