set shell := ["bash", "-euo", "pipefail", "-c"]
set positional-arguments := true

# List the available development commands.
default:
    @just --list

# Enter the Nix development shell (includes just and the build toolchain).
shell:
    nix develop

# Fetch all pinned engine and editor sources (requires network once).
deps:
    python3 scripts/fetch_dependencies.py

# Configure a CMake preset (debug, asan, or release).
configure preset="debug":
    cmake --preset "$1"

# Configure and build a preset.
build preset="debug": (configure preset)
    cmake --build --preset "$1" --parallel "${VSIM_BUILD_JOBS:-4}"

# Rediscover the current toolchain/dependencies and rebuild after environment changes.
rebuild preset="debug":
    cmake --fresh --preset "$1"
    cmake --build --preset "$1" --parallel "${VSIM_BUILD_JOBS:-4}"

# Build and run automated tests.
test preset="debug": (build preset)
    ctest --preset "$1"

# Check formatting, generated scenes, compilation, and tests.
check preset="debug":
    bash scripts/check.sh "$1"

# Play Crystal Courier 3D (WASD/arrows move, Space jumps, R restarts, Escape quits).
game *args: (build "debug")
    ./build/debug/vsim_game --scene examples/crystal_courier_3d/scene.json "$@"

# Run the original terminal example for headless development.
game-cli: (build "debug")
    python3 examples/crystal_courier/play.py --runtime build/debug/vsim_runtime

alias run := game

# Open the graphical scene editor. Optional arguments include --validate.
editor scene="examples/crystal_courier_3d/scene.json" *args: build-editor
    ./build/debug/vsim_editor --scene "$1" "${@:2}"

# Explicitly enable the GUI even after a headless development configuration.
build-editor:
    cmake --preset debug -DVSIM_BUILD_EDITOR=ON -DVSIM_BUILD_3D=ON -DVSIM_BUILD_EDITOR_GUI=ON
    cmake --build --preset debug --parallel "${VSIM_BUILD_JOBS:-4}"

# Start the JSON command service for agents and headless authoring.
editor-cli scene="examples/crystal_courier_3d/scene.json": (build "debug")
    @echo 'VSim command editor: enter {"op":"scene"} or {"op":"describe"}. Ctrl-D exits.' >&2
    ./build/debug/vsim_editor_cli --scene "$1"

# Run a scene for a fixed number of ticks without the terminal frontend.
runtime scene="examples/crystal_courier_3d/scene.json" ticks="120" preset="debug": (build preset)
    "./build/$3/vsim_runtime" --scene "$1" --ticks "$2"

# Run the engine API conformance scene.
conformance: (build "debug")
    ./build/debug/vsim_runtime --scene scenes/conformance.json --ticks 120

# Check GLFW/Vulkan availability in the current desktop session.
probe: (build "debug")
    ./build/debug/vsim_platform_probe

# Regenerate embedded scenes after changing their Lua sources or layout.
generate:
    python3 scripts/generate_conformance.py
    python3 examples/crystal_courier/generate.py
    python3 examples/crystal_courier_3d/generate.py

# Exercise actual window presentation and resizing (requires a desktop session).
test-window: (build "debug")
    ./build/debug/vsim_renderer_tests --window

# Run GPU tests through Mesa's software Vulkan driver from the Nix shell.
test-software preset="debug": (build preset)
    VK_DRIVER_FILES="${VSIM_SOFTWARE_VULKAN_ICD:?Enter the updated Nix shell first}" ctest --preset "$1"

# Install a release runtime and its scenes without editor executables.
install prefix="dist/runtime": (build "release")
    cmake --install build/release --prefix "$1"

# Build the headless engine core and install the SDK: headers, static libraries,
# the vsim CMake package, and the vsim.pc pkg-config file (no graphics or editor).
install-sdk prefix="dist/sdk": (build "sdk")
    cmake --install build/sdk --prefix "$1"

# Test the headless core (physics, scenes, Lua, conformance, installation).
test-sdk: (build "sdk")
    ctest --preset sdk
