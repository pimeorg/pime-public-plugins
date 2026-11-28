#!/usr/bin/env python3
"""Assemble scheme packages from built plugin packages.

    python tools/pack_schemes.py --packages build/wasm-ndk/package \
                                 --out dist/schemes [--only pinyin english]

Each directory under schemes/win/ is one scheme package:

    schemes/win/<id>/
      scheme.yaml   # id, name, version, chain file, plugin (id, version) list
      chain.yaml    # the chain recipe, compiled with tools/chainc

The output directory gets scheme.yaml, <chain>.cbor and copies of every
plugin package the manifest pins, so the package runs without the repo:

    dist/schemes/<id>/
      scheme.yaml
      <chain>.cbor
      plugins/pime-plugin-<plugin-id>-<version>/...

The pinned (id, version) must already be built into --packages; a missing
package fails the run instead of producing a scheme that cannot load.
"""

from __future__ import annotations

import argparse
import shutil
import subprocess
import sys
from pathlib import Path

import yaml

ROOT = Path(__file__).resolve().parent.parent
CHAINC = ROOT / "tools" / "chainc"


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--schemes", default=str(ROOT / "schemes" / "win"),
                        help="directory of scheme packages (default schemes/win)")
    parser.add_argument("--packages", required=True,
                        help="directory of built plugin packages")
    parser.add_argument("--out", default=str(ROOT / "dist" / "schemes"),
                        help="output directory (default dist/schemes)")
    parser.add_argument("--only", nargs="*", default=None,
                        help="only these scheme ids")
    args = parser.parse_args()

    schemes_dir = Path(args.schemes)
    packages_dir = Path(args.packages)
    out_dir = Path(args.out)
    if not packages_dir.is_dir():
        sys.exit(f"pack_schemes: no package directory {packages_dir}")

    dirs = sorted(p for p in schemes_dir.iterdir()
                  if p.is_dir() and (p / "scheme.yaml").is_file())
    if args.only:
        dirs = [p for p in dirs if p.name in args.only]
        missing = set(args.only) - {p.name for p in dirs}
        if missing:
            sys.exit(f"pack_schemes: no scheme {', '.join(sorted(missing))}")
    if not dirs:
        sys.exit(f"pack_schemes: no scheme packages under {schemes_dir}")

    for scheme_dir in dirs:
        scheme = yaml.safe_load((scheme_dir / "scheme.yaml").read_text("utf-8"))
        scheme_id = str(scheme.get("id", ""))
        chain = str(scheme.get("chain", ""))
        plugins = scheme.get("plugins") or []
        if scheme_id != scheme_dir.name or not chain or not plugins:
            sys.exit(f"pack_schemes: {scheme_dir}/scheme.yaml needs id "
                     f"'{scheme_dir.name}', chain and plugins")

        missing = [f"pime-plugin-{p['id']}-{p['version']}" for p in plugins
                   if not (packages_dir /
                           f"pime-plugin-{p['id']}-{p['version']}").is_dir()]
        if missing:
            sys.exit(f"pack_schemes: {scheme_id} needs unbuilt packages: "
                     f"{', '.join(missing)}")

        out = out_dir / scheme_id
        shutil.rmtree(out, ignore_errors=True)
        (out / "plugins").mkdir(parents=True)

        shutil.copyfile(scheme_dir / "scheme.yaml", out / "scheme.yaml")
        subprocess.run([sys.executable, str(CHAINC),
                        str(scheme_dir / "chain.yaml"),
                        "-o", str(out / chain)], check=True)
        for plugin in plugins:
            name = f"pime-plugin-{plugin['id']}-{plugin['version']}"
            shutil.copytree(packages_dir / name, out / "plugins" / name)

        print(f"scheme {scheme_id}: {chain} + {len(plugins)} plugin(s) -> {out}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
