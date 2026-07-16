#!/usr/bin/env python3
"""Fail when private enterprise implementation leaks into the public ent core."""

from __future__ import annotations

import argparse
import sys
from pathlib import Path

PUBLIC_HEADERS = {
    "ent_comm.h",
    "ent_db.h",
    "ent_init.h",
    "ent_log.h",
    "ent_msg.h",
    "ent_script.h",
    "ent_shm.h",
    "ent_thread.h",
    "ent_types.h",
    "ent_utility.h",
}

FORBIDDEN_PATH_PARTS = {
    "ent-enterprise",
    "ent_enterprise",
    "advanced_shm",
    "election",
    "fencing",
    "replication",
}

SOURCE_ROOTS = ("comm", "inc", "cmake", "example", "test", "test_optional")
FORBIDDEN_IDENTIFIERS = (
    "ent::enterprise",
    "ENT_ENTERPRISE_",
    "EE_ADVSHM_",
    "EE_REPL_",
    "EE_PEP_",
)


def iter_files(root: Path):
    for path in root.rglob("*"):
        if not path.is_file() or ".git" in path.parts:
            continue
        yield path


def check(root: Path) -> list[str]:
    findings: list[str] = []

    for path in iter_files(root):
        relative = path.relative_to(root)
        lowered_parts = {part.lower() for part in relative.parts}
        leaked_parts = lowered_parts & FORBIDDEN_PATH_PARTS
        if leaked_parts:
            findings.append(
                f"forbidden private path component {sorted(leaked_parts)}: {relative}"
            )

    include_dir = root / "inc"
    if include_dir.is_dir():
        for header in include_dir.glob("*.h"):
            if header.name not in PUBLIC_HEADERS:
                findings.append(f"unexpected public header: {header.relative_to(root)}")

    source_paths: list[Path] = []
    for directory in SOURCE_ROOTS:
        candidate = root / directory
        if candidate.is_dir():
            source_paths.extend(iter_files(candidate))
    for candidate in (root / "CMakeLists.txt", root / "cmake"):
        if candidate.is_file():
            source_paths.append(candidate)

    for path in source_paths:
        if path.suffix.lower() not in {".c", ".h", ".cmake", ".txt", ".in"}:
            continue
        try:
            text = path.read_text(encoding="utf-8")
        except UnicodeDecodeError:
            findings.append(f"non-UTF-8 source metadata: {path.relative_to(root)}")
            continue
        for identifier in FORBIDDEN_IDENTIFIERS:
            if identifier in text:
                findings.append(
                    f"private identifier {identifier!r}: {path.relative_to(root)}"
                )

    return sorted(set(findings))


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--root", type=Path, default=Path(__file__).resolve().parents[1])
    args = parser.parse_args()
    root = args.root.resolve()

    findings = check(root)
    if findings:
        print("public-scope check failed:", file=sys.stderr)
        for finding in findings:
            print(f"- {finding}", file=sys.stderr)
        return 1

    print(f"public-scope check passed: {root}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
