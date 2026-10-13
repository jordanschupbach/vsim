---
name: vsim-validation
description: Review and validate VSim changes before committing, pushing, or claiming release readiness. Covers native tests, sanitizer checks, runtime isolation, and backend evidence.
---

Read `AGENTS.md` and `docs/status.md`. Review changed code before committing,
focusing on resource ownership, failed mutation rollback, malformed scene data,
Lua longjmp boundaries, and whether tests exercise observable behavior.

Run the debug, asan, and release variants of `bash scripts/check.sh`. Install the
release build to a temporary prefix, verify that only `vsim_runtime` and (with 3D
enabled) `vsim_game` were installed, and run both the installed conformance scene
and offscreen graphical example. Inspect dynamic dependencies before claiming
a distributable package; a binary with `/nix/store` references needs its closure.

Run `vsim_platform_probe` in a desktop session for platform changes. Its success
proves surface/device discovery only, not rendering. Renderer release gates must
include Vulkan validation, image readback/reference comparison, resize and
minimize/restore, resource churn, and driver coverage. The `renderer` CTest suite
checks image properties, offscreen resizing, and device lifetime. `just test-window`
checks presentation/resizing on an accessible desktop. Minimize/restore, physical
driver coverage, and manual input checks remain separate release gates. Use
`just test-software` to select Mesa explicitly; do not override the user's normal
game driver or describe software rendering as hardware certification.

When a check cannot run, record its exact blocker and distinguish it from a
passing check. Review `git diff --check` and staged files before committing.
Inspect the remote branch before pushing; do not overwrite unknown history.
Existing user authorization to commit/push remains valid. This skill adds no
approval requirement.
