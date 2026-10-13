# VSim development

Read `README.md` for the actual implementation status before promising engine features.
VSim is the provisional name pending the owner's selection. The product brief is
`initial_prompt.md`. Linux is the first target; Windows and macOS are not certified.

## Architecture and invariants

- C++20, CMake, static `vsim_core`. Editor commands live in `vsim_editor_core` and
  must never be linked into `vsim_runtime`. Keep platform/GPU dependencies out of
  headless scene and behavior tests.
- GLM owns math conventions: meters, seconds, right-handed, Y up, camera looks
  down -Z. Quaternion storage is `[w,x,y,z]`; camera depth is Vulkan zero to one.
- Scene mutation goes through validated APIs. IDs are positive signed-64-bit
  compatible integers, monotonically allocated within a scene snapshot. Never
  retain IDs across load/undo as if they belonged to the new snapshot.
- Component changes must update serialization, strict validation, protocol
  discovery, Lua access, tests, and the conformance scene together. Unknown
  fields are errors; do not silently discard newer scene data.
- Lua callbacks are trusted project code, isolated per entity. Use protected
  calls; do not longjmp over live C++ objects. Keep host file/process access out
  of the exposed libraries. See `docs/scripting.md` for lifecycle semantics.
- Editor mutations are transactional. Failed requests must preserve the scene
  and history. stdout is exclusively one JSON response per input line.
- RAII owns resources. No owning raw pointers, global mutable engine singletons,
  homegrown physics substitutes, silent feature fallbacks, or unchecked GPU calls.
- NVRHI/Vulkan and Jolt are implemented; their contracts and limits are in
  `docs/graphics-physics.md`. Miniaudio, glTF, IBL, SSAO, and the graphical editor
  remain integration milestones. Require executable evidence for backend claims.
- Preserve NVRHI's negative-height viewport convention and keep depth/shadow/HUD
  orientation image tests passing. One Renderer owns the static Vulkan-Hpp
  dispatcher at a time. Release NVRHI resources before native Vulkan objects.
- Keep `Jolt/Jolt.h` before other Jolt headers. Gameplay runs through `Simulation`
  (scripts followed by physics); body edits/removal must reconcile before queries.

## Style and verification

- Format C++ with the checked-in `.clang-format`. Types use PascalCase;
  functions/variables use snake_case; private members end in `_`.
- Public headers include their own dependencies. Apply warnings and sanitizers
  to project targets, without imposing them on third-party targets.
- Add behavioral unit tests for new engine behavior and regression tests for
  bugs. Cover invalid input, boundaries, resource lifetime, and failure recovery.
  Do not use `assert` as the test runner: release builds must still check results.
- Run `bash scripts/check.sh debug`, `asan`, and `release` in the Nix dev shell.
  A skipped GPU check is not a passing renderer check. Record environmental
  limitations, compiler versions, and actual results.
- Before any commit, review the entire change for correctness, lifetime,
  serialization compatibility, thread assumptions, style, and test coverage.
  Inspect `git diff --check` and staged content; exclude generated builds,
  credentials, local caches, and downloaded assets without provenance.
- Commit only reviewed, passing work. The owner authorized pushing to
  `https://github.com/jordanschupbach/vsim`; inspect existing remote history first,
  preserve it, and never force-push. Permission/network restrictions must be
  reported, not worked around by relocating protected Git metadata.

## Repository skills

The session protects `.agents` and `.codex` from writes, so repository skills live
under `skills/`. Read the applicable entry directly:

- `skills/vsim-development/SKILL.md`: changing engine components, commands, or Lua.
- `skills/vsim-validation/SKILL.md`: review, testing, and release readiness.
