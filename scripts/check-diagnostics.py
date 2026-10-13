#!/usr/bin/env python3
"""Reject new GCC/Clang warnings while preserving an explicit baseline."""
import argparse
from collections import Counter
import json
from pathlib import Path
import re


def diagnostics(path):
    counts = Counter()
    # Locations vary with source edits and archive extraction directories.
    pattern = re.compile(r"([^/\\\s:]+\.(?:c|h)):\d+:\d+: warning: (.*)")
    msvc = re.compile(r"([^/\\\s]+\.(?:c|h))\(\d+(?:,\d+)?\): warning (C\d+): (.*?)(?: \[.*\])?$")
    for line in Path(path).read_text(errors="replace").splitlines():
        match = pattern.search(line)
        if match:
            counts[f"{match[1]}: {match[2]}"] += 1
        else:
            match = msvc.search(line)
            if match:
                counts[f"{match[1]}: {match[2]}: {match[3]}"] += 1
    return counts


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("before")
    parser.add_argument("after")
    parser.add_argument("--output", required=True)
    args = parser.parse_args()
    before, after = diagnostics(args.before), diagnostics(args.after)
    added = after - before
    report = {"baseline": dict(before), "current": dict(after), "new": dict(added)}
    Path(args.output).write_text(json.dumps(report, indent=2) + "\n")
    print(f"Warnings: baseline={sum(before.values())}, current={sum(after.values())}, new={sum(added.values())}")
    for warning, count in added.items():
        print(f"NEW ({count}): {warning}")
    return 1 if added else 0


if __name__ == "__main__":
    raise SystemExit(main())
