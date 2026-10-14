# VSim inside Mep

Keep this project at `engines/vsim/` in the Mep repository. The whole engine,
graphical and command editors, examples (including the Lua orbit camera), tests,
development guidelines, and pinned dependencies belong together. VSim remains a
standalone CMake project with its own Nix shell; Mep and VSim currently use
different compiler configurations. The editor runs in its own process and Mep's
existing `:exec` support adopts its native window into a pane.

## Build and open a pane on Linux

After transferring the source, build once from the engine directory:

```sh
cd ~/projects/mep/engines/vsim
nix develop path:. --command just deps
nix develop path:. --command just build-editor
```

`path:.` also includes source files that have not yet been added to Mep's Git
index. The transferred dependency cache satisfies `just deps` without downloads;
it verifies the pinned revisions and clean worktrees. A fresh setup without that
cache needs network access. Nix still needs its toolchain closure. Do not copy
old `build/` or `.direnv/` directories: their absolute paths refer to the original
checkout. Run `direnv allow` in the new directory if using direnv.

In Mep, open the editor as the current pane:

```text
:exec bash /home/jordan/projects/mep/engines/vsim/scripts/mep-editor.sh
```

When Mep's workspace is the Mep repository, a relative path also works:

```text
:exec bash engines/vsim/scripts/mep-editor.sh
:exec bash engines/vsim/scripts/mep-editor.sh "games/my game/scene.json" --validate
```

The default scene is Crystal Courier 3D. An optional first argument selects
another scene; remaining arguments are editor flags. Custom scene and capture
paths resolve against Mep's workspace. The launcher uses `exec` to retain the
child PID and propagate its exit status. It launches a prebuilt editor immediately;
building inside `:exec` could exceed Mep's window-discovery timeout.
`just editor-pane [scene.json] [flags...]` runs the same launcher from the engine
directory. `just editor` and `just game` still build and run standalone windows.

Use **Ctrl-\** or click outside the editor to return keyboard focus to Mep. Click
the editor or press **Enter** to return focus to it. Mep handles moving, splitting,
resizing, and hiding the pane. After closing the editor, **r** restarts it. Save
before `:bd`: Mep requests a graceful close but eventually stops an unresponsive
child, so a pending editor save prompt cannot keep the pane alive indefinitely.

The launcher passes `--x11` explicitly. Both Mep and the editor must use the same
accessible X11 display (including XWayland). Native Wayland windows cannot be
adopted by Mep's X11 backend. `--x11` is incompatible with `--offscreen` and fails
clearly if X11 is unavailable. Windows/macOS pane integration is not certified.

## Transfer and verification

Copy all source files, dotfiles, documentation, skills, scenes, shaders, and tests
into the new engine directory. Preserve the source repository until the new
checkout is verified. Keep VSim's `.git` out of Mep; the engine becomes ordinary
files in Mep's repository. A separate Git bundle can preserve the original commit
history without introducing a nested repository. Do not overwrite existing Mep
files or merge its unrelated uncommitted changes. The nested `.github/workflows/`
files document VSim's checks; GitHub only runs workflows at the repository root,
so the prepared transfer includes `.github/workflows/vsim-linux.yml` at Mep's
root. It runs the three engine configurations from `engines/vsim/` and uploads
their test logs. Review and commit that workflow with the transferred engine.

Run `just check debug`, `just check asan`, and `just check release` inside the
engine's Nix shell. `mep_launcher` checks relocation, literal paths, workspace
retention, PID/exit propagation, and the missing-build diagnostic. GUI and
renderer suites exercise Vulkan offscreen, including rejecting incompatible
window options before GPU creation.

Desktop acceptance must also open the actual Mep pane, resize/split/switch it,
exercise Ctrl-\ and keyboard/mouse input, run Play/Stop, save a changed scene,
close the pane, and restart it. Offscreen tests do not establish native embedding
or focus behavior; these checks require an accessible desktop.
