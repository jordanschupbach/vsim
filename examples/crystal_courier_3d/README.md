# Crystal Courier 3D

Run `just game` from the repository root in the Nix shell. Move the pink sphere
with WASD or arrows; press Space to jump, R to restart, Escape to quit. Collect
all five turquoise crystals and enter the golden portal. The orange floor tile
ends the run. Progress, controls, and victory/defeat appear inside the window.

This standalone scene uses real engine components: perspective camera, procedural
box/sphere/crystal meshes, metallic/roughness materials, Jolt static/dynamic/kinematic
bodies, sensors, Lua scripts, and a spawned trophy prefab. Crates can be pushed,
the blue ball bounces, and the cyan lift moves vertically. One crystal is on a
raised platform that requires jumping. The physics tests complete the level using
only movement/jump input, without teleporting or editing gameplay state.

`generate.py` authors the level and embeds `game.lua`; run `just generate` after
editing either. The checked-in `scene.json` needs no external assets or scripts.
All geometry, material colors, and HUD glyphs are authored in the repository.
There are no downloaded art assets or runtime network requests.

```sh
just game --validate
just game --offscreen --frames 120 --capture build/courier.ppm
just editor examples/crystal_courier_3d/scene.json
build/debug/vsim_game --scene my-edited-scene.json
```

The command editor can inspect and edit the same scene, simulate with input,
and save to a new file; preview that file through `vsim_game`. `--output NEW_SCENE`
saves the final simulated scene for inspection. A scene snapshot preserves entity
components and linear velocities, not Lua locals or Jolt angular/sleep state;
loading one starts a new gameplay session, not a save-game checkpoint.

Offscreen mode requires a finite `--frames` count and still executes the real
Vulkan and Jolt backends. `--validate` requires the Khronos validation layer. Use
`just test-software` for automated checks with Nix's Mesa software driver. For
manual window testing, use the host's normal Vulkan driver.
