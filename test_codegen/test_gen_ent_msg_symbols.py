#!/usr/bin/env python3
import subprocess
import sys
import tempfile
from pathlib import Path


def main() -> int:
    if len(sys.argv) != 2:
        raise SystemExit("usage: test_gen_ent_msg_symbols.py GENERATOR")

    generator = Path(sys.argv[1])
    with tempfile.TemporaryDirectory() as temp_dir:
        root = Path(temp_dir)
        spec = root / "catalog.msg"
        header = root / "catalog.h"
        source = root / "catalog.c"
        spec.write_text(
            "module DATA 17\n"
            "submodule QUE 5 symbol ACME_QUEUE\n"
            "BAD err 1 queue failure\n"
            "EMPTY ok 2 queue empty\n"
            "submodule NET 6\n"
            "DOWN err 1 network down\n",
            encoding="utf-8",
        )
        completed = subprocess.run(
            [
                sys.executable,
                str(generator),
                "--input",
                str(spec),
                "--header",
                str(header),
                "--source",
                str(source),
                "--symbol-prefix",
                "ACME",
                "--type-prefix",
                "ACME",
                "--table-prefix",
                "g_acme_msg",
                "--include-guard",
                "ACME_MSG_GEN_H",
                "--emit-symbol-name",
            ],
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            text=True,
            check=False,
        )
        if completed.returncode != 0:
            raise AssertionError(completed.stderr)

        header_text = header.read_text(encoding="utf-8")
        source_text = source.read_text(encoding="utf-8")
        for expected in (
            "ACME_QUEUE_BAD",
            "ACME_QUEUE_EMPTY",
            "ACME_DATA_NET_DOWN",
        ):
            if expected not in header_text or expected not in source_text:
                raise AssertionError(f"missing generated symbol: {expected}")
        for forbidden in (
            "ACME_DATA_QUE_BAD",
            "ACME_DATA_QUE_EMPTY",
        ):
            if forbidden in header_text or forbidden in source_text:
                raise AssertionError(f"default symbol was not replaced: {forbidden}")

        bad_spec = root / "bad.msg"
        bad_spec.write_text(
            "module DATA 17\n"
            "submodule QUE 5 prefix ACME_QUEUE\n"
            "BAD err 1 queue failure\n",
            encoding="utf-8",
        )
        completed = subprocess.run(
            [
                sys.executable,
                str(generator),
                "--input",
                str(bad_spec),
                "--header",
                str(root / "bad.h"),
                "--source",
                str(root / "bad.c"),
            ],
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            text=True,
            check=False,
        )
        if completed.returncode == 0 or "submodule line must be" not in completed.stderr:
            raise AssertionError("invalid symbol-prefix syntax was accepted")

        collision_spec = root / "collision.msg"
        collision_spec.write_text(
            "module DATA 17\n"
            "submodule QUE 5 symbol ACME_COMMON\n"
            "BAD err 1 queue failure\n"
            "submodule NET 6 symbol ACME_COMMON\n"
            "BAD err 1 network failure\n",
            encoding="utf-8",
        )
        completed = subprocess.run(
            [
                sys.executable,
                str(generator),
                "--input",
                str(collision_spec),
                "--header",
                str(root / "collision.h"),
                "--source",
                str(root / "collision.c"),
                "--symbol-prefix",
                "ACME",
            ],
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            text=True,
            check=False,
        )
        if completed.returncode == 0 or "conflicting generated symbol 'ACME_COMMON_BAD'" not in completed.stderr:
            raise AssertionError("final generated-symbol collision was accepted")

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
