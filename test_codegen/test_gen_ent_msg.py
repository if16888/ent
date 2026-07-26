#!/usr/bin/env python3
import subprocess
import sys
import tempfile
from pathlib import Path


def run_generator(generator: Path, args, expect_success=True):
    completed = subprocess.run(
        [sys.executable, str(generator), *args],
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        text=True,
        check=False,
    )
    if expect_success and completed.returncode != 0:
        raise AssertionError(completed.stderr)
    if not expect_success and completed.returncode == 0:
        raise AssertionError("generator unexpectedly succeeded")
    return completed


def main() -> int:
    if len(sys.argv) != 3:
        raise SystemExit("usage: test_gen_ent_msg.py GENERATOR LEGACY_SPEC")

    generator = Path(sys.argv[1])
    legacy_spec = Path(sys.argv[2])

    with tempfile.TemporaryDirectory() as temp_dir:
        root = Path(temp_dir)

        legacy_h = root / "legacy.h"
        legacy_c = root / "legacy.c"
        run_generator(
            generator,
            [
                "--input",
                str(legacy_spec),
                "--header",
                str(legacy_h),
                "--source",
                str(legacy_c),
            ],
        )
        legacy_header = legacy_h.read_text(encoding="utf-8")
        if "#define ENT_SYS_NORMAL" not in legacy_header:
            raise AssertionError("legacy symbol naming changed")
        if "const char* symbol;" in legacy_header:
            raise AssertionError("legacy catalog ABI changed")

        first_h = legacy_header
        first_c = legacy_c.read_text(encoding="utf-8")
        run_generator(
            generator,
            [
                "--input",
                str(legacy_spec),
                "--header",
                str(legacy_h),
                "--source",
                str(legacy_c),
            ],
        )
        if legacy_h.read_text(encoding="utf-8") != first_h:
            raise AssertionError("header generation is not deterministic")
        if legacy_c.read_text(encoding="utf-8") != first_c:
            raise AssertionError("source generation is not deterministic")

        spec_a = root / "a.msg"
        spec_b = root / "b.msg"
        spec_a.write_text(
            "module REPL 17\n"
            "submodule QUE 5\n"
            "BAD err 1 queue failure\n",
            encoding="utf-8",
        )
        spec_b.write_text(
            "module TRAN 18\n"
            "submodule TLS 2\n"
            "BAD err 1 tls failure\n",
            encoding="utf-8",
        )
        generic_h = root / "ee_msg_gen.h"
        generic_c = root / "ee_msg_gen.c"
        run_generator(
            generator,
            [
                "--input",
                str(spec_a),
                "--input",
                str(spec_b),
                "--header",
                str(generic_h),
                "--source",
                str(generic_c),
                "--symbol-prefix",
                "EE",
                "--type-prefix",
                "EE",
                "--table-prefix",
                "g_ee_msg",
                "--include-guard",
                "EE_MSG_GEN_H",
                "--emit-symbol-name",
            ],
        )
        generic_header = generic_h.read_text(encoding="utf-8")
        generic_source = generic_c.read_text(encoding="utf-8")
        for expected in (
            "EE_REPL_QUE_BAD",
            "EE_TRAN_TLS_BAD",
            "EE_MSG_ITEM_T",
            "g_ee_msg_items",
            "const char* symbol;",
        ):
            if expected not in generic_header and expected not in generic_source:
                raise AssertionError(f"missing generic output: {expected}")

        duplicate = root / "duplicate.msg"
        duplicate.write_text(
            "module REPL 17\n"
            "submodule QUE 5\n"
            "OTHER err 1 duplicate code\n",
            encoding="utf-8",
        )
        failed = run_generator(
            generator,
            [
                "--input",
                str(spec_a),
                "--input",
                str(duplicate),
                "--header",
                str(root / "duplicate.h"),
                "--source",
                str(root / "duplicate.c"),
                "--symbol-prefix",
                "EE",
            ],
            expect_success=False,
        )
        if "duplicate encoded value" not in failed.stderr:
            raise AssertionError("duplicate code failure was not diagnostic")

        invalid = root / "invalid.msg"
        invalid.write_text(
            "module TEST 20\n"
            "submodule API 1\n"
            "BAD fatal 1 invalid severity\n",
            encoding="utf-8",
        )
        failed = run_generator(
            generator,
            [
                "--input",
                str(invalid),
                "--header",
                str(root / "invalid.h"),
                "--source",
                str(root / "invalid.c"),
            ],
            expect_success=False,
        )
        if "severity must be 'ok' or 'err'" not in failed.stderr:
            raise AssertionError("invalid severity failure was not diagnostic")

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
