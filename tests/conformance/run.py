#!/usr/bin/env python3
"""Repository conformance: layout, manifests, chains and package presence.

    python tests/conformance/run.py [--root .] [--strict-packages]

This is the CI-facing umbrella check. It runs tools/check_plugins, verifies
every chain lock with tools/chainc, and (when build/package exists) confirms
that each plugin produced a package directory containing its manifest.
"""

from __future__ import annotations

import argparse
import subprocess
import sys
from pathlib import Path

try:
    import yaml
except ImportError:  # pragma: no cover - environment guard
    sys.exit("conformance: PyYAML is required (pip install pyyaml)")


def run(command: list[str]) -> int:
    print("$", " ".join(command))
    return subprocess.call(command)


def main() -> int:
    parser = argparse.ArgumentParser(description="pime plugin conformance")
    parser.add_argument("--root", default=str(Path(__file__).resolve().parents[2]))
    parser.add_argument("--strict-packages", action="store_true",
                        help="fail when a built package is missing (needs a wasm toolchain)")
    args = parser.parse_args()

    root = Path(args.root).resolve()
    python = sys.executable
    failures = 0

    failures += run([python, str(root / "tools" / "check_plugins")]) != 0

    chains = sorted(path for path in (root / "chains").rglob("*.yaml")
                    if not path.name.endswith(".lock.yaml"))
    for chain in chains:
        failures += run([python, str(root / "tools" / "chainc"), str(chain),
                         "--check"]) != 0

    package_root = root / "build" / "native" / "package"
    built = {path.name for path in package_root.iterdir() if path.is_dir()} \
        if package_root.exists() else set()
    if built:
        for manifest_path in sorted((root / "plugins").glob("*/*/manifest.yaml")):
            plugin_dir = manifest_path.parent
            if plugin_dir.name.startswith("_"):
                continue
            manifest = yaml.safe_load(manifest_path.read_text(encoding="utf-8"))
            expected = f"pime-plugin-{manifest.get('id')}-{manifest.get('version')}"
            if expected not in built:
                message = f"package {expected} not found in {package_root}"
                if args.strict_packages:
                    print(f"error: {message}")
                    failures += 1
                else:
                    print(f"warning: {message} (no wasm toolchain?)")
    else:
        print("note: no built packages; skipping package presence check")

    print("conformance:", "ok" if failures == 0 else f"{failures} failure(s)")
    return 0 if failures == 0 else 1


if __name__ == "__main__":
    raise SystemExit(main())
