# Implementation status and release gates

VSim now runs a real 3D example on Linux. This does not certify the full engine
from the original product brief. See the actual results in [validation](validation.md).

| Area | Current evidence / required work |
|---|---|
| C++ static core, CMake, separate editor/runtimes | Implemented and covered by installation/protocol tests |
| Entities, camera, mesh, rigid body, strict scenes | Validated components, round trips, prefab and undo tests |
| Lua lifecycle and physics API | 18 bindings; conformance scene covers every binding |
| Crystal Courier 3D | Perspective arena, jumping, crates, bouncing ball, moving lift, sensors, victory/restart; input-only winning route tested |
| GLFW/Vulkan/NVRHI | Offscreen rendering/readback/resize/device lifetime tested on Mesa llvmpipe; window presentation implemented but local desktop access blocked |
| PBR/HDR/shadows | Procedural meshes, metallic/roughness GGX, RGBA16F, ACES-fitted tonemap, directional PCF shadow map |
| SSAO | 32 hemisphere samples, world-space radius, depth/normal-aware filtering, indirect-light composition; offscreen image and validation tests pass |
| glTF/textures, HDRI/IBL | Not implemented |
| Jolt physics | Real static/dynamic/kinematic bodies, box/sphere, CCD, sensors, ground queries, impulses, Lua control |
| Agent editing | Tested transactional batches, persistent play/stop previews, selection, shared camera/focus/pick commands, dirty state, undo/redo, atomic save replacement; CLI is `vsim_editor_cli` |
| Graphical editor | Dear ImGui/NVRHI panels, inspector, ImGuizmo, picking/navigation, undo and play/stop compiled; offscreen image and mouse interaction tests pass; desktop checks remain |
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
3. Add bounded glTF/textures import, HDR environment preprocessing/IBL,
   configurable lighting, and larger-world shadows. Extend visual scenes/tests
   for each feature. Poly Haven assets need cached provenance and package notices.
4. Add character-controller/contact-event APIs, robust physics state checkpoints,
   and miniaudio with device-loss/headless tests. Current shape queries and scene
   snapshots are not full collision event streams or save-game state.
5. Validate the graphical editor on a desktop, including high-DPI, resize,
   minimize/restore, keyboard focus, save prompts, and input across physical GPUs.
   Offscreen GUI rendering and injected mouse interaction tests now pass.
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

## Active goal: finish initial_prompt.md

The user authorized continuing through the whole brief. The current work order is:

1. Graphical editor integration passed debug and ASan/UBSan builds and offscreen
   tests after the owner fetched dependencies. Nix archive hashes and source
   overrides are recorded. Desktop and package-build certification remain open.
2. Add bounded glTF/material/texture assets with fixtures, import diagnostics,
   editor assignment, scripting discovery, and rendering tests.
3. Add HDRI/IBL and lighting controls with image regressions and asset provenance.
   SSAO is implemented and verified on software Vulkan; hardware quality/performance checks remain.
4. Integrate miniaudio with components, Lua, scene conformance, and device/headless tests.
5. Add game export, package notices, clean-machine validation, and platform builds.
6. Complete release gates, review, and commit/push when repository/network access permits.

No untested platform is counted as complete. The current Git baseline is
`989e7ce`; the goal's changes remain uncommitted.

The previous missing-source blocker was cleared on 2026-10-13 by the owner's
`just deps`. All seven dependencies match their pinned revisions and have clean
source trees. The editor now builds and passes its offscreen UI suites. Miniaudio
and stb are fetched but are not yet integrated into engine features.

Current environmental limits remain: GLFW cannot open display `:0`; Nix cannot
write its user cache or access the daemon from the managed session; `.git` is
protected from writes; and GitHub DNS is inaccessible. The cached direnv shell
still supports local compilation and software Vulkan tests. These limits do not
reduce the original objective or certify outstanding features.
