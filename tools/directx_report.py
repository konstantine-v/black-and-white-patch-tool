"""Summarise BWDecomp.directx.csv (written with [Diagnostics] TraceDirectX=1).

Usage: directx_report.py <BWDecomp.directx.csv> [functions.csv] [--markdown]

Prints every DirectDraw/Direct3D method the game called with its total count, the number of call sites and the
original functions that call it most (named from functions.csv). --markdown writes a table for docs/RENDERER.md.
"""

from __future__ import annotations

import bisect
import collections
import csv
import sys


def load_functions(path: str) -> tuple[list[int], list[str]]:
    starts, names = [], []
    with open(path, newline="", encoding="utf-8") as f:
        for row in csv.DictReader(f):
            starts.append(int(row["address"], 16))
            names.append(row["name"])
    return starts, names


def main() -> None:
    args = [a for a in sys.argv[1:] if not a.startswith("--")]
    markdown = "--markdown" in sys.argv
    starts, names = load_functions(args[1]) if len(args) > 1 else ([], [])

    def function_of(caller: int) -> str:
        i = bisect.bisect_right(starts, caller) - 1
        return names[i] if i >= 0 else f"0x{caller:08x}"

    totals: dict[str, int] = collections.Counter()
    callers: dict[str, collections.Counter] = collections.defaultdict(collections.Counter)
    with open(args[0], newline="", encoding="utf-8") as f:
        for row in csv.DictReader(f):
            key = f"{row['interface']}::{row['method']}"
            count = int(row["count"])
            totals[key] += count
            where = function_of(int(row["caller"], 16)) if row["in_game"] == "1" else "(BWDecomp/system)"
            callers[key][where] += count

    if markdown:
        print("| Method | Calls | Main callers |")
        print("|---|---:|---|")
    for key in sorted(totals):
        top = ", ".join(f"{name} ({count})" for name, count in callers[key].most_common(3))
        if markdown:
            print(f"| `{key}` | {totals[key]:,} | {top} |")
        else:
            print(f"{totals[key]:>12,}  {key:50} {top}")


if __name__ == "__main__":
    main()
