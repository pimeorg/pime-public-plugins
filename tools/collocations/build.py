#!/usr/bin/env python3
"""Build a character-collocation table from the pinyin dictionary.

    python tools/collocations/build.py data/pinyin/pinyin.dict data/pinyin/collocations.tsv

For every multi-character word the adjacent character pairs are counted and
weighted by the word frequency, which approximates the character n-gram the
sentence decoder uses to prefer natural word boundaries. Output is
`<char1><char2>\\t<count>`, sorted by the first character then count.
"""
from __future__ import annotations

import argparse
import collections
import sys
from pathlib import Path


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("dict", type=Path)
    parser.add_argument("out", type=Path)
    parser.add_argument("--limit", type=int, default=40000)
    args = parser.parse_args()

    pairs: collections.Counter[tuple[str, str]] = collections.Counter()
    words = 0
    for line in args.dict.read_text(encoding="utf-8").splitlines():
        if not line or line.startswith("#"):
            continue
        cols = line.split("\t")
        if len(cols) < 3:
            continue
        text = cols[1]
        try:
            freq = int(cols[2])
        except ValueError:
            continue
        chars = list(text)
        if len(chars) < 2:
            continue
        words += 1
        for left, right in zip(chars, chars[1:]):
            pairs[(left, right)] += freq

    ordered = sorted(pairs.items(), key=lambda kv: (-kv[1], kv[0]))
    ordered = ordered[: args.limit]
    lines = [
        "# Character collocations from the pinyin dictionary (build tool).",
        "# Format: <char1><char2>\\t<weight>",
    ]
    lines += [f"{a}{b}\t{weight}" for (a, b), weight in ordered]
    args.out.write_text("\n".join(lines) + "\n", encoding="utf-8")
    print(f"collocations: {words} words, {len(ordered)} pairs -> {args.out}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
