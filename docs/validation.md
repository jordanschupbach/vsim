# Linux validation history — 2026-10-13

The latest graphical editor validation is recorded at the end of this document.
Earlier entries describe the capabilities and constraints at their respective
milestones; they are retained as historical evidence.

## Initial Linux validation

This record covers the implemented headless foundation. It does not certify the
unimplemented renderer, physics, audio, graphical editor, or portable game export.

## Local toolchain

NixOS x86_64 Linux; CMake 3.31.6; Ninja 1.13.2; GCC 14.4.0 for final debug,
sanitizer, and release builds (core also passed on GCC 14.2.1); GLM 1.0.2;
nlohmann JSON 3.12.0; Lua 5.4.7; GLFW 3.4;
Vulkan headers/loader 1.4.328. Tools and libraries came from the existing Nix store.
Host-specific paths were supplied through local environment/CMake cache values,
not embedded in project build files. The flake pins the host's reported nixpkgs
revision, but its full build is still blocked by Nix daemon access.

## Results

| Check | Observed result |
|---|---|
| `bash scripts/check.sh debug` | 5/5 CTest suites passed |
| ASan + UBSan | 5/5 suites passed with `ASAN_OPTIONS=detect_leaks=0 UBSAN_OPTIONS=halt_on_error=1` |
| `bash scripts/check.sh release` | 4/4 suites passed; editor disabled in this configuration |
| Core behavior | 46 named C++ test cases passed, including release builds |
| Editor/runtime protocol | 13 Python integration tests passed in debug and sanitizer builds |
| Installed runtime | Installed to a fresh temporary prefix; ran 120 conformance ticks; only runtime installed |
| Conformance | All 10 Lua bindings exercised; transform/camera/script components and prefabs exercised; source synchronization passed |
| Formatting | clang-format dry-run passed through the check script |
| Skills | Both repository skills passed the skill-creator validator |
| Configuration | CMake presets JSON and CI YAML parsed; Nix syntax parsed |
| Whitespace | Per-file `git diff --no-index --check` passed for generated source/config/docs |

CTest logs remain under `build/{debug,asan,release}/Testing/Temporary/` and are
intentionally ignored by Git. CI is configured to retain its own test reports.

## Review performed before any commit

Manually reviewed the complete initial C++ implementation, build configuration,
tests, and documented contracts. Reviewed ownership, Lua/C++ error boundaries,
script lifecycle ordering, serialization compatibility, transactional editing,
numeric limits, output publication, and runtime/editor separation.

Findings fixed and regression-tested:

- Reject duplicate JSON keys and excessive nesting before accepting commands.
- Reject out-of-range numeric conversion and invalid UTF-8 before scene mutation.
- Compute matrices with double intermediates and reject unrepresentable results.
- Keep allocating Lua return data in owned instance storage; unwind C++ exceptions
  before Lua errors and prohibit engine calls during shutdown/reentrant API calls.
- Preserve scenes/history when loading, editing, or simulation fails.
- Enforce the same encoded scene-size limit during editing and saving.
- Publish only complete files without overwriting an existing path, including
  competing writers; verify temporary files are cleaned up.
- Preserve dependency search paths for installed runtime binaries. The first
  installation smoke test caught missing Lua at startup; CMake install RPATH was
  corrected and installation is now an automated CTest suite.

The current design deliberately documents trusted scripts, scene-scoped IDs,
synchronous single-threaded simulation, no power-loss durability guarantee, and
Nix runtime closure requirements. Those are practical limits of this foundation,
not production certification.

## Blocked checks and publication

- **LeakSanitizer:** default sanitizer runs fail with `LeakSanitizer has encountered
  a fatal error` and the process-inspection/ptrace diagnostic in this managed
  session. Leak detection was disabled only for local execution; it remains
  enabled by default in project configuration and CI. Leak checking has not passed.
- **GPU:** after selecting compatible cached compiler/libraries, the probe runs
  and reports `X11: Failed to open display :0`. No successful surface/GPU run is
  claimed. Earlier cached-library architecture/libc mismatches were resolved by
  using GCC 14.4 consistently, including a fresh debug build. Use a consistent
  Nix development shell on a desktop host.
- **Nix:** the daemon socket returns `Operation not permitted`. The flake syntax
  is valid, but `nix develop`, `nix flake check`, and generation of `flake.lock`
  could not complete here. Package evaluation/build and CI are unverified.
- **Dependencies:** NVRHI, Jolt, and miniaudio were not present in the local cache;
  shell DNS/network restrictions prevented fetching them. No stub backends were
  substituted.
- **Git:** `git init -b main .` failed because `.git` is a read-only filesystem.
  No repository initialization, commit, or push was completed. Read-only
  `git ls-remote https://github.com/jordanschupbach/vsim.git` also failed with
  `Could not resolve host: github.com`; remote history remains unknown.

Resume with normal Git/Nix/network access, inspect existing remote history,
generate and review the flake lock, run the default sanitizer suite and Nix CI,
then review the final diff before committing and pushing. The owner already
authorized those operations. Renderer/platform checks additionally need an
accessible desktop and driver.

## Just workflow and Crystal Courier follow-up — 2026-10-13

Added the root `justfile`, Nix-shell `just` dependency, Crystal Courier scene/Lua
sources and terminal frontend, normalized script input, persistent interactive
runtime requests, and editor scene preloading. `just game` was played through an
18-move victory, and `just editor` loaded the same game and simulated input.

Latest checks on GCC 14.4.0 / just 1.43.1:

- `just check debug`: 8/8 CTest suites passed.
- `just check release`: 6/6 suites passed, with the editor excluded.
- `ASAN_OPTIONS=detect_leaks=0 UBSAN_OPTIONS=halt_on_error=1 just check asan`:
  8/8 suites passed; the earlier leak-inspection restriction still applies.
- 51 core cases, 13 editor protocol tests, 10 game/runtime/frontend tests, and
  2 recipe integration tests passed. Both generated-scene synchronization checks
  and installed-runtime checks passed, including player input to the installed
  example scene.
- `just --unstable --fmt --check`, C++ formatting, Git whitespace checks, and
  Nix syntax validation passed. Full Nix evaluation/CI remains unverified.

Reviewed input validation before script execution, frame lifetime/reset semantics,
fatal handling of partially executed callbacks, subprocess cleanup, editor undo
after input simulation, and literal recipe argument forwarding. Recipe integration
tests use paths containing spaces, quotes, and shell command syntax to verify they
remain literal paths. Game tests cover victory, defeat, walls, a locked exit,
restart, EOF, invalid requests, and bounded terminal coordinates after editing.
The terminal renders scene state; Lua owns gameplay rules.

No renderer, graphical editor, or physics backend is implied by the terminal game.
GitHub remote inspection was retried and still failed to resolve `github.com`.

## Nix game startup repair — 2026-10-13

Reproduced `just game` through `direnv exec .`: the runtime failed before entering
the game with `__nptl_change_stack_perm, version GLIBC_PRIVATE`. CMake retained
compiler and dependency paths from the earlier GCC 14.4 fallback environment
while the actual flake shell uses GCC 14.3. Reconfigured the debug, asan, and
release builds from fresh CMake caches inside the cached direnv development shell.
The debug runtime now uses that shell's compiler, Lua, and matching glibc 2.40-224
loader/library paths, verified in the CMake cache and ELF metadata.

Added `just rebuild [preset]` to reset cached toolchain/dependency discovery and
build after an environment change. A regression test uses two dependency roots
and real CMake/just processes to verify that normal builds retain the old cached
path and rebuilding selects the new dependency, for debug and release presets.
Reviewed argument quoting, preset selection, and the documented reset of manual
CMake cache overrides; no runtime code or library-path workarounds were added.

Validation in `direnv exec .` (GCC 14.3.0, CMake 4.1.2):

- `just game`: full winning session and clean quit passed.
- `just editor`: preloaded scene startup and `describe` command passed.
- `just check debug`: 8/8 CTest suites passed, including all 3 recipe tests.
- `just check release`: 6/6 CTest suites passed, including installed runtime checks.
- `ASAN_OPTIONS=detect_leaks=0 UBSAN_OPTIONS=halt_on_error=1 just check asan`:
  8/8 suites passed. Leak detection remains unverified under the existing sandbox
  restriction; no project sanitizer defaults were weakened.
- C++/just formatting, generated scene checks, and Git whitespace checks passed.

The user-created `flake.lock` is now present. These checks used the available
cached direnv shell; they do not establish that `nix flake check` or CI passed.

## Crystal Courier 3D milestone — 2026-10-13

This section supersedes the earlier statements that renderer/Jolt sources were
unavailable. The owner ran `just deps`; all three exact revisions are now cached
and verified. Linux now has an actual NVRHI/Vulkan renderer and Jolt simulation,
with a standalone 3D scene and `just game` launching the graphical executable.

Toolchain: cached Nix development environment, GCC 14.3.0, CMake 4.1.2, Lua 5.4.7,
GLFW 3.4, Vulkan loader 1.4.328, pinned Vulkan-Headers 1.4.352, Jolt 5.3.0, and the
NVRHI commit recorded in `dependencies.json`. GPU execution used Mesa llvmpipe
25.2.6 / LLVM 21.1.7 (software Vulkan), with Khronos and NVRHI validation enabled.

Checks and evidence:

- Debug: 12/12 CTest suites passed. Release: 10/10 passed with the editor disabled.
- ASan + UBSan: 12/12 suites passed with `ASAN_OPTIONS=detect_leaks=0` and
  `UBSAN_OPTIONS=halt_on_error=1`, including real Jolt collision and Vulkan rendering.
  The engine and Jolt are instrumented; external driver libraries are not.
- A separate configure with `VSIM_BUILD_3D=OFF` and
  `VSIM_BUILD_PLATFORM_PROBE=OFF` passed all 10 headless suites without a GPU.
- 51 core, 19 physics/gameplay, 9 renderer, 14 editor protocol, 10 terminal/runtime,
  2 graphical executable/recipe, and 3 other recipe cases passed: 108 named cases
  in debug, plus generated-scene/conformance/installation checks.
- The complete 3D game renders for 120 frames with zero validation errors. The
  captured image was inspected. The final local preview is
  `build/debug/crystal-courier-3d.png` (ignored build artifact).
- Renderer checks use observable image properties with tolerances: near/far depth
  occlusion and color channels, expected shadow location, HDR emission/tonemapping,
  Y orientation, HUD placement, odd-width readback, resize, frame/resource churn,
  invalid lifecycle calls, and complete device teardown/recreation. They are not
  a claim of pixel-identical rendering across vendors.
- Physics checks cover box/sphere gravity/contact, sleeping queries, mass/impulses,
  CCD against a thin wall, restitution, dynamic pushing, sensors, kinematic motion,
  teleport/resize/removal, prefab independence, repeated spawn/destruction, Lua
  error unwinding, jump/no-double-jump, win/loss, and restart. One test wins the
  actual authored level using only movement/jump input, without teleportation.
- `just game` and `just run` execute the graphical runtime, capture images, and
  save simulated scenes through automated tests with literal shell-like filenames.
  `just editor` was separately checked to preload the 3D scene and expose Jolt.
- Temporary-prefix installation runs both installed executables and checks that
  editor executables are absent and backend notices are present. ELF inspection
  confirms Lua/GLFW/Vulkan and Nix library closure requirements remain.
- C++/just formatting, scene generation checks, Nix syntax, CI YAML parsing,
  repository skill validation, and Git whitespace checks passed.

Review covered the new code, changed runtime/editor paths, shader layouts/math,
component validation/serialization, build options, dependency provenance, and
installed content. Findings fixed during implementation/review included:

- NVRHI's negative-height viewport convention, shadow lookup orientation, and HUD
  orientation; image tests now guard those conventions.
- Volatile constant-buffer layout matching and explicitly enabled Vulkan dynamic
  rendering; GPU validation now runs without errors.
- Frame completion before resource reuse, presentation semaphores per swapchain
  image, idle-before-resize, and native object destruction after NVRHI resources.
- Explicitly reject overlapping renderers using the static Vulkan-Hpp dispatcher.
- Query sleeping Jolt bodies through real narrowphase/ray queries; contact-listener
  activity alone cannot reliably implement grounded state after sleep.
- Reject an oversized impulse before native mutation, release native bodies if
  map insertion fails, and reject invalid/overflowing screenshot dimensions.
- Normalize validated, rounded scene quaternions at the Jolt boundary; its unit
  tolerance is tighter than the scene format. Teleport tests cover this case.
- Keep Jolt's required first header ahead of its other headers under clang-format.
- Enable Jolt C++ RTTI for UBSan virtual-call instrumentation in sanitizer builds.
  UBSan then exposed Jolt 5.3's null-filter union aliasing. Registering Jolt's public
  pass-through simulation filter preserves collision behavior using its correctly
  typed wrapper path; no vptr sanitizer suppression or upstream source edits.
- Keep the moving lift above the floor and visually distinguish the hazard from
  the goal after HDR tonemapping.

Remaining environmental/release gates:

- Window execution: the host desktop is inaccessible (`Failed to open display :0`).
  Xvfb also fails to bind local/Unix sockets in this sandbox. No successful window
  presentation, manual input, minimize/restore, high-DPI, or physical GPU run is
  claimed. `just test-window` and a CI Xvfb step are provided for those environments;
  minimize/restore and interactive controls additionally need manual verification.
- LeakSanitizer retains the earlier process-inspection restriction. Local ASan
  execution uses `detect_leaks=0`; CI/default sanitizer behavior is not weakened.
- `nix eval --offline --no-write-lock-file path:.#devShells.x86_64-linux.default.drvPath`
  fails because the Nix daemon socket is not permitted. Full flake evaluation/build,
  remote source archive hash validation, and CI are unverified.
- GitHub remote inspection still fails with `Could not resolve host: github.com`.
  After review and passing checks, `git add --all` failed to create `.git/index.lock`
  with `Read-only file system`. No commit or push was possible.


## Graphical editor integration in progress (2026-10-13)

The frontend is not yet a verified deliverable. New Dear ImGui 1.91.9b and ImGuizmo
1.9 source revisions are pinned in `dependencies.json`. Fetching them from this
session fails with `Could not resolve host: github.com`. The user has been asked
to run `just deps`. Frontend compilation, its new `graphical_editor` suite, GUI
screenshots, desktop input, gizmo interaction, and Nix source hashes are pending.

The independently buildable editor model and renderer extension have executable
evidence, using the existing Nix GCC 14.3/CMake 4.1 toolchain and Mesa llvmpipe:

- `bash scripts/check.sh debug`: 13/13 suites passed with
  `VSIM_BUILD_EDITOR_GUI=OFF` explicitly configured.
- `bash scripts/check.sh asan`: 13/13 suites passed with GUI off and local
  `ASAN_OPTIONS=detect_leaks=0`, `UBSAN_OPTIONS=halt_on_error=1`. Leak checking is
  still blocked by this sandbox's process-inspection restrictions.
- `bash scripts/check.sh release`: 10/10 suites passed, editor excluded.
- New coverage checks transactional batches and redo preservation, persistent
  Lua/Jolt play previews, stop/error recovery without authored-scene changes,
  selection lifetime, dirty state, explicit atomic save replacement, primitive
  picking, clipping, nonuniform transforms, and editor camera/viewport rendering.
- Runtime installation tests still verify both installed executables, scene runs,
  software Vulkan validation, and exclusion of editor executables.

`just editor` now explicitly enables the graphical target; it will require the
new sources rather than accidentally launching a stale command-editor binary.
The JSON command executable and recipe are now `vsim_editor_cli` and `editor-cli`.
No changes from this integration have been committed or pushed while the GUI is
uncompiled. Passing the above tests does not imply GUI, driver, or platform certification.


## Shared viewport commands and SSAO (2026-10-13)

The CLI and GUI now share `view`, `set_view`, `focus`, and `pick` through the
editor model. Protocol tests verify camera initialization, focus framing, picking,
invalid request rollback, play-mode restrictions, and independence from authored
scene history. The graphical adapter still awaits dependency fetch/compilation.

SSAO is implemented in the existing NVRHI pipeline and enabled by default. GPU
readbacks verify open-plane visibility, contact darkening, unaffected background,
disable/zero-strength behavior, and final lighting composition. Existing shadow,
viewport orientation, resize, resource churn, and runtime install tests still pass.
A binding-register collision found by NVRHI validation during development was fixed
before the passing runs. AO constants use b1; frame constants use b0.

- Debug: 13/13 CTest suites passed, with GUI explicitly excluded.
- ASan/UBSan: 13/13 passed, with GUI excluded and the previously documented local
  LeakSanitizer exception. Release: 10/10 passed, editor excluded.
- The example ran for 12 offscreen frames at 960x600 on llvmpipe with zero validation
  errors. The resulting game and AO diagnostic captures were visually inspected.
- New GUI shaders compile with glslc; this is not a GUI C++ build or GUI test pass.

Generated evidence is under `build/debug/courier-ssao.png` and
`build/debug/ssao-contacts.png` and is intentionally not tracked. Physical driver,
window interaction, and cross-platform evidence are still required. miniaudio and
stb pins were added to the pending explicit dependency fetch for later milestones;
this does not claim audio or image-loader integration.

## Graphical editor built and exercised (2026-10-13)

This entry supersedes the earlier missing-GUI-source blocker. After the owner ran
`just deps`, all seven cached dependency HEADs matched `dependencies.json` and
their worktrees were clean. Dear ImGui and ImGuizmo now compile into the separate
`vsim_editor` executable. Nix hashes for the four newly fetched sources were
computed from clean `git archive` trees; GUI source overrides were added to the
flake. The remote fixed-output downloads and complete Nix package build remain
unverified in this session.

Validation uses the cached Nix shell: GCC 14.3.0, CMake 4.1.2, and Mesa llvmpipe
25.2.6. The following checks passed with actual Vulkan/NVRHI rendering:

- Debug: 15/15 CTest suites, including `graphical_editor` and `editor_interaction`.
- ASan/UBSan: 15/15 suites with `ASAN_OPTIONS=detect_leaks=0` and
  `UBSAN_OPTIONS=halt_on_error=1`. The existing leak-inspection restriction remains;
  no sanitizer defaults were weakened.
- Release runtime configuration: 10/10 suites, with editor targets disabled.
- An additional optimized build with the editor enabled passed both GUI suites.
  The release preset was then restored to its runtime-only configuration.
- The complete `just editor examples/crystal_courier_3d/scene.json --offscreen
  --frames 3 --validate --capture build/debug/editor.ppm` recipe built and rendered
  successfully, reporting zero validation errors. Its converted capture at
  `build/debug/editor.png` was visually inspected.
- GUI tests drive actual ImGui mouse events to create a cube, select it by mesh
  picking, drag the X-axis gizmo, verify a single undo entry, undo/redo, move the
  editor camera, and start/stop play without changing the authored scene. Renderer
  validation is checked after GUI teardown as well as during rendering.
- Process-level GUI tests cover saved scenes, persistent simulation previews,
  command-file validation, empty scenes, text/viewport/inspector image content,
  and literal paths containing shell metacharacters through the real just recipe.
- C++ formatting, just formatting, Nix syntax parsing, generated scene checks,
  runtime installation, and Git whitespace checks passed.

Review found and fixed an ImGuizmo header ordering requirement and a memory read
past the GUI index array. NVRHI rounds short Vulkan uploads to four-byte multiples;
an odd number of 16-bit ImGui indices previously left two missing source bytes.
The sanitizer GUI test caught this, and initialized padding now protects the
upload. The failing test passes with the fix. Review also synchronized the editor
view immediately after a GUI load or console command and avoided busy polling
while the native window is minimized.

Remaining evidence limits: the real desktop launch fails with `X11: Failed to
open display :0`. Offscreen input injection does not validate GLFW event delivery,
window focus, DPI scaling, or physical GPU drivers. Direnv falls back to its
previous cached shell because Nix cannot write the managed session's user cache;
the new flake was syntax checked but not built. Remote Git inspection still cannot
resolve GitHub, and protected `.git` prevents publication. glTF/textures, HDRI/IBL,
miniaudio integration, portable export, and Windows/macOS validation remain in the
original goal; this editor milestone does not claim those features.

## Example camera orbit (2026-10-13)

Crystal Courier 3D's Camera entity now embeds `camera.lua`. It orbits at six
degrees per second, preserves its authored height/radius and lens, and aims at
the arena center. The new `engine.set_rotation(id, w, x, y, z)` binding uses the
existing validated scene mutation path, retaining the entity's other components.
API discovery, scripting documentation, and the conformance scene cover it.

Review checked quaternion multiplication/order, delta-based motion, bounded
angle accumulation, script-source retention, and scene generation. Tests cover
invalid quaternion arguments without mutation, a complete orbit and wraparound,
center aim, floor framing at game/editor aspect ratios, timestep independence,
game execution, and camera changes during editor Play with authored-state
restoration after Stop.

All required check scripts passed in the cached Nix environment: debug 15/15,
ASan/UBSan 15/15 (the documented local `detect_leaks=0` limitation), and runtime
release 10/10. An offscreen editor capture after 900 simulation ticks showed the
expected quarter-orbit view with zero Vulkan validation errors; the image was
visually inspected at `build/debug/camera-orbit-15s.png`.
