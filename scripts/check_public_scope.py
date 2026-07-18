#!/usr/bin/env python3
"""Fail when private enterprise implementation leaks into public source or SDKs."""

from __future__ import annotations

import argparse
import sys
import tempfile
from pathlib import Path
from typing import Iterable

PUBLIC_HEADERS = {
    "ent_comm.h",
    "ent_db.h",
    "ent_init.h",
    "ent_log.h",
    "ent_msg.h",
    "ent_msg_gen.h",
    "ent_script.h",
    "ent_shm.h",
    "ent_thread.h",
    "ent_types.h",
    "ent_utility.h",
}

FORBIDDEN_PATH_TOKENS = (
    "ent-enterprise",
    "ent_enterprise",
    "advanced_shm",
    "replication",
    "election",
    "fencing",
    "libevent",
    "libev",
    "rpc_adapter",
)

FORBIDDEN_IDENTIFIERS = (
    "ent::enterprise",
    "ENT_ENTERPRISE_",
    "EE_ADVSHM_",
    "EE_REPL_",
    "EE_PEP_",
)

TEXT_METADATA_SUFFIXES = {
    ".c", ".cmake", ".h", ".in", ".json", ".md", ".pc", ".ps1",
    ".py", ".sh", ".toml", ".txt", ".yaml", ".yml",
}

# This checker contains the forbidden identifiers it enforces. Its rule table
# is not an enterprise leak and must not cause the checker to reject itself.
IDENTIFIER_SCAN_EXEMPT_PATHS = {Path("scripts/check_public_scope.py")}


def iter_files(root: Path) -> Iterable[Path]:
    for path in root.rglob("*"):
        if path.is_file() and ".git" not in path.parts:
            yield path


def check_header_surface(root: Path, findings: list[str]) -> None:
    for directory_name in ("inc", "include"):
        include_dir = root / directory_name
        if not include_dir.is_dir():
            continue
        for header in include_dir.rglob("*.h"):
            relative = header.relative_to(root)
            if (len(relative.parts) != 2 or
                    relative.parts[0] != directory_name or
                    header.name not in PUBLIC_HEADERS):
                findings.append(
                    f"unexpected public header: {relative}"
                )


def check_text_metadata(root: Path, findings: list[str]) -> None:
    for path in iter_files(root):
        relative = path.relative_to(root)
        if relative in IDENTIFIER_SCAN_EXEMPT_PATHS:
            continue
        if path.suffix.lower() not in TEXT_METADATA_SUFFIXES:
            continue
        try:
            text = path.read_text(encoding="utf-8")
        except UnicodeDecodeError:
            findings.append(f"non-UTF-8 source metadata: {path.relative_to(root)}")
            continue
        for identifier in FORBIDDEN_IDENTIFIERS:
            if identifier in text:
                findings.append(
                    f"private identifier {identifier!r}: {relative}"
                )


def private_path_tokens(relative: Path) -> list[str]:
    matches: set[str] = set()
    for part in relative.parts:
        lowered = part.lower()
        for token in FORBIDDEN_PATH_TOKENS:
            if token in lowered:
                matches.add(token)
    return sorted(matches)


def check(root: Path) -> list[str]:
    findings: list[str] = []

    if not root.is_dir():
        return [f"scope root does not exist or is not a directory: {root}"]

    for path in iter_files(root):
        relative = path.relative_to(root)
        leaked_tokens = private_path_tokens(relative)
        if leaked_tokens:
            findings.append(
                f"forbidden private path token {leaked_tokens}: {relative}"
            )

    check_header_surface(root, findings)
    check_text_metadata(root, findings)
    return sorted(set(findings))


def run_self_test() -> int:
    with tempfile.TemporaryDirectory() as temporary_directory:
        root = Path(temporary_directory)
        include_dir = root / "include"
        include_dir.mkdir()
        (include_dir / "ent_shm.h").write_text("/* public */\n", encoding="utf-8")
        if check(root):
            print("public-scope self-test failed: public header was rejected", file=sys.stderr)
            return 1

        (root / "private-leak.md").write_text("ENT_ENTERPRISE_SECRET\n", encoding="utf-8")
        if not check(root):
            print("public-scope self-test failed: Markdown private identifier was accepted", file=sys.stderr)
            return 1
        (root / "private-leak.md").unlink()

        (root / "private-leak.yml").write_text("EE_REPL_PRIVATE\n", encoding="utf-8")
        if not check(root):
            print("public-scope self-test failed: YAML private identifier was accepted", file=sys.stderr)
            return 1
        (root / "private-leak.yml").unlink()

        nested_header = include_dir / "private"
        nested_header.mkdir()
        (nested_header / "ent_shm.h").write_text("/* private */\n", encoding="utf-8")
        if not check(root):
            print("public-scope self-test failed: nested same-name header was accepted", file=sys.stderr)
            return 1

    print("public-scope self-test passed")
    return 0


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument(
        "--root",
        type=Path,
        default=Path(__file__).resolve().parents[1],
        help="source checkout or extracted runtime/devel package root",
    )
    parser.add_argument(
        "--self-test",
        action="store_true",
        help="run checker regression tests",
    )
    args = parser.parse_args()
    if args.self_test:
        return run_self_test()
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
