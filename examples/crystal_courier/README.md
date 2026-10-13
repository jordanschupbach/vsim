# Crystal Courier

A playable, turn-based example built with VSim's actual Lua/scene/prefab APIs.
Collect all three crystals (`*`), avoid traps (`X`), and reach the exit (`E`).
Walls (`#`) block movement; your courier is `@`.

```sh
nix develop
just game-cli
```

Use **W/A/S/D then Enter** to move, **R** to restart, or **Q** to quit. EOF exits
cleanly. The terminal frontend works without a window, GPU, or extra Python
packages. This is the original terminal example. Run `just game` for Crystal Courier 3D.

```sh
just editor examples/crystal_courier/scene.json
```

The editor opens the same scene and accepts JSON commands. For example:

```json
{"op":"scene"}
{"op":"simulate","ticks":1,"actions":{"move_x":1}}
{"op":"undo"}
{"op":"save","path":"my-level.json"}
```

`save` requires a new path. `simulate` starts fresh Lua instances for each command,
as documented in the editor protocol. Play through the runtime to retain a full
game session, including the Lua move count and victory state.

The authored files are:

- `layout.txt`: the enclosed grid (`P` player, `*` crystal, `E` exit, `X` trap).
- `game.lua`: all movement, wall checks, collection, loss, and victory rules.
- `generate.py`: embeds the level and Lua source into `scene.json`.
- `play.py`: sends input to `vsim_runtime --interactive` and draws returned entities.

After editing the map or Lua, run `just generate`, then `just test`. CTest checks
that generated scenes match their sources and plays both winning and losing
sessions against the compiled runtime. The generator requires a rectangular map
enclosed by walls, one player, and one exit. The terminal supports up to 128 by 128
tiles with nonnegative integer X/Z coordinates, within the engine's entity limit.

Entity names identify roles in this small example. Each grid object uses a regular
Transform component on the X/Z plane. A named `Status: ...` entity supplies the
text displayed by the terminal. The camera is authored for a future top-down
viewport. Winning spawns a Trophy prefab; collecting crystals and hitting a trap
exercise entity destruction. Grid movement is game logic, not a physics backend.
