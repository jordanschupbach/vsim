#!/usr/bin/env python3
"""Fetch exact upstream commits explicitly; ordinary CMake configuration stays offline."""
import json
from pathlib import Path
import subprocess
import sys
import tempfile


def git(folder, *arguments):
    return subprocess.run(["git", "-C", str(folder), *arguments], check=True,
                          text=True, stdout=subprocess.PIPE).stdout.strip()


def main():
    root = Path(__file__).resolve().parents[1]
    dependencies = json.loads((root / "dependencies.json").read_text(encoding="utf-8"))
    cache = root / ".cache/dependencies"
    cache.mkdir(parents=True, exist_ok=True)
    for name, spec in dependencies.items():
        revision = spec["revision"]
        destination = cache / f"{name}-{revision}"
        if not destination.exists():
            print(f"Fetching {name} at {revision}", flush=True)
            with tempfile.TemporaryDirectory(prefix=f"{name}-", dir=cache) as temporary:
                checkout = Path(temporary) / "source"
                checkout.mkdir()
                git(checkout, "init", "--quiet")
                git(checkout, "fetch", "--depth=1", spec["url"], revision)
                git(checkout, "checkout", "--quiet", "--detach", "FETCH_HEAD")
                if git(checkout, "rev-parse", "HEAD") != revision:
                    raise RuntimeError(f"unexpected revision for {name}")
                checkout.rename(destination)
        if git(destination, "rev-parse", "HEAD") != revision:
            raise RuntimeError(f"unexpected revision in {destination}")
        if git(destination, "status", "--porcelain", "--untracked-files=normal"):
            raise RuntimeError(f"dependency checkout has local changes: {destination}")
        print(f"Ready: {name} ({revision[:12]})", flush=True)
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (OSError, RuntimeError, subprocess.CalledProcessError) as error:
        print(f"Dependency setup failed: {error}", file=sys.stderr)
        raise SystemExit(1)
