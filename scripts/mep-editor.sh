#!/usr/bin/env bash
# Launch the prebuilt editor as an X11 child that Mep's :exec can adopt.
set -euo pipefail
engine_root="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd -P)"
editor="$engine_root/build/debug/vsim_editor"
if [[ ! -x "$editor" ]]; then
    printf 'VSim editor has not been built. From %s, run: nix develop path:. --command just build-editor\n' "$engine_root" >&2
    exit 1
fi
# Resolve the default scene relative to the engine, but preserve Mep's working
# directory so explicit scene and --capture paths remain workspace-relative.
scene="$engine_root/examples/crystal_courier_3d/scene.json"
if [[ $# -gt 0 && "$1" != --* ]]; then
    scene="$1"
    shift
fi
exec "$editor" --x11 --scene "$scene" "$@"
