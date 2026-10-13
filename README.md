# VSim

Linux-first C++20 engine with a playable **Crystal Courier 3D** example, a
Vulkan/NVRHI renderer, Jolt physics, Lua behavior, and an agent-facing command editor.
VSim is the provisional name. This is an implemented and tested engine milestone;
the full production engine described in `initial_prompt.md` is still in development.

## Play on Linux / Nix

```sh
nix develop          # Or use the .envrc with direnv allow
just deps            # Fetch pinned sources once; already done in this workspace
just game            # Build and open Crystal Courier 3D
```

Move the pink courier with **WASD / arrow keys**, **Space** to jump, **R** to restart,
and **Escape** to quit. Collect five turquoise crystals and enter the golden portal.
Jump onto the raised platform, push crates, ride the lift, and avoid the orange hazard.
The game has actual 3D meshes, perspective/depth rendering, metallic/roughness
materials, filtered directional shadows, HDR tonemapping, and Jolt rigid bodies.
Gameplay runs in Lua at 60 Hz. See [the example](examples/crystal_courier_3d/README.md).

`just game --validate` additionally enables NVRHI and Vulkan validation. A Vulkan
1.3 driver and accessible desktop session are required for the window. There is
an explicit offscreen mode for testing and agent screenshots:

```sh
just game --offscreen --frames 120 --validate --capture build/courier.ppm
just editor          # JSON command editor, preloaded with the 3D scene
just game-cli        # Original terminal example, retained for headless work
```

The editor currently exposes a JSON command interface; its graphical viewport and
transform gizmos remain future work. An agent can author mesh/body/material/script
components, simulate physics, save a scene, and preview it with `vsim_game --scene PATH`.

## Development commands

The `.envrc` loads the flake's default shell. Run `direnv allow` once after enabling
direnv in your shell. CMake configuration performs no network downloads; `just deps`
verifies exact revisions and rejects modified cached sources. The Nix package uses
the same pinned revisions and source hashes from `dependencies.json`.

| Command | Action |
|---|---|
| `just game [arguments...]` / `just run` | Build and launch Crystal Courier 3D |
| `just editor [scene.json]` | Open the command editor with the scene loaded |
| `just game-cli` | Run the terminal example |
| `just build [preset]` | Configure and compile debug, asan, or release |
| `just rebuild [preset]` | Reset cached toolchain/dependency paths and rebuild |
| `just test [preset]` | Run all CTest suites, including offscreen GPU checks |
| `just test-software [preset]` | Run tests through Mesa software Vulkan in the Nix shell |
| `just test-window` | Test real window presentation, resizing, and image readback |
| `just check [preset]` | Check formatting, generated scenes, build, and tests |
| `just runtime [scene.json] [ticks] [preset]` | Simulate a scene without graphics |
| `just conformance` | Exercise every exposed Lua API and component |
| `just generate` | Regenerate embedded scenes after Lua/level edits |
| `just install [prefix]` | Build/install release runtimes, scenes, and backend notices |

After changing toolchains, run `just rebuild` inside the new shell. CMake caches
absolute compiler/library paths; mixing Nix environments can cause a
`GLIBC_PRIVATE` startup error. `rebuild` also resets manual cache overrides; keep
persistent overrides in `CMakeUserPresets.json`.

An ordinary CMake environment needs a C++20 compiler, CMake 3.25+, Ninja, GLM 1.0+,
nlohmann JSON 3.11+, Lua 5.4, GLFW 3.4, Vulkan loader 1.3+, `glslc`, Python 3.10+,
clang-format, and `just`, plus the pinned sources fetched with `just deps`.
For a fully headless build, configure with `-DVSIM_BUILD_3D=OFF` and
`-DVSIM_BUILD_PLATFORM_PROBE=OFF`; Jolt physics and core tests still run. GPU tests
are labeled `gpu`. Excluding them does not establish renderer correctness.

The flake pins nixpkgs in `flake.lock`. In an untracked initial checkout, use
`nix develop path:.` if Git filtering hides new sources. Full Nix package/CI
execution remains unverified in this restricted session; see [validation](docs/validation.md).

## Agent editing and runtime separation

`vsim_editor` accepts one JSON request per line, exposes discovery with
`{"op":"describe"}`, and supports transactional scene changes, simulation,
undo/redo, prefabs, and saving. See [the protocol](docs/editor-protocol.md) and
[Lua API](docs/scripting.md). `vsim_runtime --interactive` provides persistent
headless simulation/input for automation. `vsim_game` adds graphics and desktop input.
Both runtimes link the static engine core without editor code.

```sh
just install
./dist/runtime/bin/vsim_game --scene dist/runtime/share/vsim/examples/crystal_courier_3d/scene.json
```

Installation includes both runtimes, conformance and example scenes, embedded
shaders, and Jolt/NVRHI/Vulkan header notices. The editor is excluded. This is a
development installation: Nix binaries need their runtime closure. Portable game
export and clean-machine distribution validation remain future work.

The current renderer uses procedural geometry and a fixed directional light.
glTF/textures, HDRI/IBL, SSAO, audio, graphical editing, and Windows/macOS validation
are not implemented. See [status and release gates](docs/status.md),
[renderer/physics contracts](docs/graphics-physics.md), and [AGENTS.md](AGENTS.md).
