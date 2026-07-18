#!/usr/bin/env python3
"""Fail when private enterprise implementation leaks into public source or SDKs."""

from __future__ import annotations

import argparse
import os
import re
import stat
import sys
import tarfile
import tempfile
import zipfile
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

FORBIDDEN_PERSONAL_CONTACT_SUFFIXES = (
    "@" + "foxmail" + ".com",
)

MAX_ARCHIVE_MEMBER_BYTES = 64 * 1024 * 1024

SECRET_PATTERNS = (
    ("private key", re.compile(rb"-----BEGIN [A-Z0-9 ]*PRIVATE KEY-----")),
    ("GitHub token", re.compile(rb"(?:ghp|gho|ghu|ghs|ghr)_[A-Za-z0-9]{30,}")),
    ("GitHub fine-grained token", re.compile(rb"github_pat_[A-Za-z0-9_]{40,}")),
    ("AWS access key", re.compile(rb"(?:AKIA|ASIA)[A-Z0-9]{16}")),
)

# This checker contains the forbidden identifiers it enforces. Its rule table
# is not an enterprise leak and must not cause the checker to reject itself.
IDENTIFIER_SCAN_EXEMPT_PATHS = {Path("scripts/check_public_scope.py")}


def iter_files(root: Path) -> Iterable[Path]:
    for path in root.rglob("*"):
        if path.is_file() and ".git" not in path.parts:
            yield path


def is_reparse_point(path: Path) -> bool:
    attributes = getattr(path.lstat(), "st_file_attributes", 0)
    reparse_attribute = getattr(stat, "FILE_ATTRIBUTE_REPARSE_POINT", 0x400)
    return bool(attributes & reparse_attribute)


def scan_payload(label: str, payload: bytes, findings: list[str]) -> None:
    lowered = payload.lower()
    for identifier in FORBIDDEN_IDENTIFIERS:
        if identifier.encode("utf-8").lower() in lowered:
            findings.append(f"private identifier {identifier!r}: {label}")
    for suffix in FORBIDDEN_PERSONAL_CONTACT_SUFFIXES:
        if suffix.encode("utf-8").lower() in lowered:
            findings.append(f"personal contact domain {suffix!r}: {label}")
    for description, pattern in SECRET_PATTERNS:
        if pattern.search(payload):
            findings.append(f"possible {description}: {label}")


def scan_archive(path: Path, findings: list[str]) -> None:
    def scan_member(archive_label: str, member_name: str, payload: bytes) -> None:
        member = Path(member_name.replace("\\", "/"))
        tokens = private_path_tokens(member)
        if member.is_absolute() or ".." in member.parts:
            findings.append(f"unsafe archive member path: {archive_label}!{member_name}")
        if tokens:
            findings.append(
                f"forbidden private path token {tokens}: {archive_label}!{member_name}"
            )
        if len(payload) > MAX_ARCHIVE_MEMBER_BYTES:
            findings.append(f"archive member exceeds scan limit: {archive_label}!{member_name}")
            return
        scan_payload(f"{archive_label}!{member_name}", payload, findings)

    try:
        if zipfile.is_zipfile(path):
            with zipfile.ZipFile(path) as archive:
                for member in archive.infolist():
                    if member.is_dir():
                        continue
                    if member.file_size > MAX_ARCHIVE_MEMBER_BYTES:
                        findings.append(f"archive member exceeds scan limit: {path}!{member.filename}")
                        continue
                    scan_member(str(path), member.filename, archive.read(member))
        elif tarfile.is_tarfile(path):
            with tarfile.open(path, mode="r:*") as archive:
                for member in archive.getmembers():
                    if member.issym() or member.islnk():
                        findings.append(f"archive link is not allowed: {path}!{member.name}")
                        continue
                    if not member.isfile():
                        continue
                    extracted = archive.extractfile(member)
                    if extracted is not None:
                        scan_member(str(path), member.name, extracted.read(MAX_ARCHIVE_MEMBER_BYTES + 1))
    except (OSError, tarfile.TarError, zipfile.BadZipFile) as error:
        findings.append(f"archive could not be inspected: {path}: {error}")


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


def check_file_contents(root: Path, findings: list[str]) -> None:
    for path in iter_files(root):
        relative = path.relative_to(root)
        if relative in IDENTIFIER_SCAN_EXEMPT_PATHS:
            continue
        try:
            payload = path.read_bytes()
        except OSError as error:
            findings.append(f"file could not be inspected: {relative}: {error}")
            continue
        scan_payload(str(relative), payload, findings)
        if zipfile.is_zipfile(path) or tarfile.is_tarfile(path):
            scan_archive(path, findings)


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

    for path in root.rglob("*"):
        if ".git" in path.parts:
            continue
        if path.is_symlink() or is_reparse_point(path):
            findings.append(f"link or reparse point is not allowed: {path.relative_to(root)}")

    for path in iter_files(root):
        relative = path.relative_to(root)
        leaked_tokens = private_path_tokens(relative)
        if leaked_tokens:
            findings.append(
                f"forbidden private path token {leaked_tokens}: {relative}"
            )

    check_header_surface(root, findings)
    check_file_contents(root, findings)
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

        (root / "extensionless").write_text("EE_PEP_PRIVATE\n", encoding="utf-8")
        if not check(root):
            print("public-scope self-test failed: extensionless leak was accepted", file=sys.stderr)
            return 1
        (root / "extensionless").unlink()

        (root / "private-leak.html").write_text("<p>ENT_ENTERPRISE_SECRET</p>\n", encoding="utf-8")
        if not check(root):
            print("public-scope self-test failed: HTML leak was accepted", file=sys.stderr)
            return 1
        (root / "private-leak.html").unlink()

        archive_path = root / "release.zip"
        with zipfile.ZipFile(archive_path, "w") as archive:
            archive.writestr("assets/details.xml", "EE_ADVSHM_PRIVATE")
        if not check(root):
            print("public-scope self-test failed: archived leak was accepted", file=sys.stderr)
            return 1
        archive_path.unlink()

        (root / "credential.bin").write_bytes(b"prefix AKIAABCDEFGHIJKLMNOP suffix")
        if not check(root):
            print("public-scope self-test failed: credential pattern was accepted", file=sys.stderr)
            return 1
        (root / "credential.bin").unlink()

        link_path = root / "linked-public-header"
        try:
            os.symlink(include_dir / "ent_shm.h", link_path)
        except (OSError, NotImplementedError):
            pass
        else:
            if not check(root):
                print("public-scope self-test failed: symbolic link was accepted", file=sys.stderr)
                return 1
            link_path.unlink()

        (root / "personal-contact.c").write_text(
            "/* contact: private" + "@foxmail" + ".com */\n", encoding="utf-8"
        )
        if not check(root):
            print("public-scope self-test failed: personal contact was accepted", file=sys.stderr)
            return 1
        (root / "personal-contact.c").unlink()

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
