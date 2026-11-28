#!/usr/bin/env python3
"""Add or disable one plugin in a scheme's chain record.

    python tools/scheme_plugins.py add pinyin pime-my-rank
    python tools/scheme_plugins.py remove pinyin pime-my-rank

`add` never edits an existing entry: the new plugin lands after the predecessor
named by its manifest `after`, or -- when that is absent or not in the chain --
after the last entry with the same role, or at the end. `remove` keeps the
entry as a disabled tombstone so the recorded order and other anchors survive.

Only the targeted chain file changes; plugin manifests and other plugins are
untouched.
"""

from __future__ import annotations

import argparse
import re
import sys
from pathlib import Path

try:
    import yaml
except ImportError:  # pragma: no cover - environment guard
    sys.exit("scheme_plugins: PyYAML is required (pip install pyyaml)")

ID_RE = re.compile(r"id:\s*([a-z0-9][a-z0-9._-]*)")


def load_yaml(path: Path):
    with path.open("r", encoding="utf-8") as handle:
        return yaml.safe_load(handle)


def entry_spans(lines: list[str]) -> list[tuple[int, int]]:
    """Line ranges of the inline `- { ... }` entries under `plugins:`."""
    spans = []
    in_plugins = False
    index = 0
    while index < len(lines):
        line = lines[index]
        if line.strip() == "plugins:":
            in_plugins = True
            index += 1
            continue
        if in_plugins:
            stripped = line.strip()
            if stripped and not line[:1].isspace() and not stripped.startswith("- {"):
                break
            if stripped.startswith("- {"):
                depth = 0
                end = index
                while end < len(lines):
                    depth += lines[end].count("{") - lines[end].count("}")
                    if depth == 0:
                        break
                    end += 1
                spans.append((index, end))
                index = end + 1
                continue
        index += 1
    return spans


def entry_id(lines: list[str], span: tuple[int, int]) -> str:
    match = ID_RE.search(lines[span[0]])
    return match.group(1) if match else ""


def find_manifest(root: Path, plugin_id: str) -> Path:
    matches = list((root / "plugins").glob(f"*/{plugin_id}/manifest.yaml"))
    if not matches:
        raise SystemExit(f"scheme_plugins: no manifest for '{plugin_id}'")
    return matches[0]


def add(root: Path, scheme: str, plugin_id: str) -> None:
    chain_path = root / "schemes" / "win" / scheme / "chain.yaml"
    manifest = load_yaml(find_manifest(root, plugin_id))
    role = str(manifest.get("role", ""))
    if not role:
        raise SystemExit(f"scheme_plugins: '{plugin_id}' has no role")

    text = chain_path.read_text(encoding="utf-8")
    lines = text.splitlines()
    spans = entry_spans(lines)
    if not spans:
        raise SystemExit(f"scheme_plugins: {chain_path} has no inline plugin entries")
    ids = [entry_id(lines, span) for span in spans]
    if plugin_id in ids:
        raise SystemExit(f"scheme_plugins: '{plugin_id}' is already in {chain_path}")

    chain = load_yaml(chain_path)
    entries = {str(item.get("id")): item for item in chain.get("plugins") or []}
    predecessor = None
    if "after" in manifest and manifest["after"] is None:
        insert_at = spans[0][0]                 # explicit chain head
    else:
        candidate = manifest.get("after")
        if candidate and candidate in entries:
            predecessor = str(candidate)
        else:
            same_role = [str(entry["id"]) for entry in chain.get("plugins") or []
                         if str(entry.get("role")) == role and not entry.get("disabled")]
            if same_role:
                predecessor = same_role[-1]
        if predecessor is None:
            insert_at = spans[-1][1] + 1        # tail
        else:
            # Insert after the last entry already anchored to this predecessor,
            # so plugins added one-by-one keep their insertion order.
            anchored = [span for span in spans
                        if str(entries[entry_id(lines, span)].get("after")) == predecessor]
            source = anchored[-1] if anchored else spans[ids.index(predecessor)]
            insert_at = source[1] + 1

    prefix = lines[spans[0][0]][: len(lines[spans[0][0]]) - len(lines[spans[0][0]].lstrip())]
    body = f"id: {plugin_id}, role: {role}"
    if predecessor is not None:
        body = f"id: {plugin_id}, after: {predecessor}, role: {role}"
    lines.insert(insert_at, f"{prefix}- {{ {body} }}")
    chain_path.write_text("\n".join(lines) + "\n", encoding="utf-8")


def remove(root: Path, scheme: str, plugin_id: str) -> None:
    chain_path = root / "schemes" / "win" / scheme / "chain.yaml"
    lines = chain_path.read_text(encoding="utf-8").splitlines()
    for span in entry_spans(lines):
        if entry_id(lines, span) != plugin_id:
            continue
        first = lines[span[0]]
        if "disabled: true" in first:
            return
        needle = f"id: {plugin_id},"
        if needle in first:
            lines[span[0]] = first.replace(
                needle, f"id: {plugin_id}, disabled: true,", 1)
        else:
            raise SystemExit(
                f"scheme_plugins: cannot mark '{plugin_id}' disabled in {chain_path}")
        chain_path.write_text("\n".join(lines) + "\n", encoding="utf-8")
        return
    raise SystemExit(f"scheme_plugins: '{plugin_id}' is not in {chain_path}")


def main() -> int:
    parser = argparse.ArgumentParser(description="Edit one plugin in a scheme chain")
    parser.add_argument("action", choices=["add", "remove"])
    parser.add_argument("scheme")
    parser.add_argument("plugin")
    parser.add_argument("--root", default=str(Path(__file__).resolve().parents[1]))
    args = parser.parse_args()
    root = Path(args.root).resolve()
    if args.action == "add":
        add(root, args.scheme, args.plugin)
    else:
        remove(root, args.scheme, args.plugin)
    print(f"{args.action} {args.plugin} in {args.scheme}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
