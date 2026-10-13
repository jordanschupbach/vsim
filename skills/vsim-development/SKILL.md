---
name: vsim-development
description: Extend VSim engine components, scene commands, or Lua behavior while preserving scene compatibility and transactional editing. Use for implementation work in this repository.
---

Read `AGENTS.md` and the relevant contract in `docs/scripting.md` or
`docs/editor-protocol.md` before changing an exposed API.

For a component, implement one validated representation in `scene.hpp` and
`scene.cpp`. Test default values, serialization round trips, invalid values,
optional removal, and prefab copy independence. Add the component to `describe`
and the conformance scene. Unknown JSON fields must remain errors.

For a Lua binding, register it in the `bindings` array; discovery derives from
that array. The conformance script checks all registered functions were called.
Test argument errors and entity destruction during callbacks. Keep C++ exception
unwinding outside Lua's longjmp error path. New script behavior must retain the
documented start/update ordering and state isolation.

For an editor mutation, construct/validate the candidate before committing. Test
that rejected operations preserve both current state and undo/redo history.
Update the protocol examples and exercise the compiled executable in Python.

Run `python3 scripts/generate_conformance.py` after editing its Lua source, then
run the applicable verification commands from `AGENTS.md`. If a dependency is
unavailable, report the blocker and keep the backend marked unavailable; do not
replace requested libraries with simulations of their behavior.
