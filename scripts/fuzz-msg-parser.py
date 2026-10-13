#!/usr/bin/env python3
"""Bounded, reproducible mutation fuzzing of the actual message parser."""
import argparse
import json
from pathlib import Path
import random
import tempfile

import gen_ent_msg as generator


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--iterations", type=int, default=2000)
    parser.add_argument("--seed", type=int, default=16888)
    parser.add_argument("--output", type=Path)
    args = parser.parse_args()
    if not 1 <= args.iterations <= 100000:
        parser.error("iterations must be 1..100000")
    rng = random.Random(args.seed)
    accepted = rejected = 0
    seeds = ["module ENT 1\nsubmodule SYS 2\nHELLO ok 3 hello\n",
             "module ABC 255\nsubmodule DEF 255\nEDGE err 32767 edge\n",
             "module ENT 0\nsubmodule SYS 0\nZERO ok 0 zero\n"]
    alphabet = "ABC_xyz0123456789 -+#\n\t\r\x00\"\\é"
    with tempfile.TemporaryDirectory(prefix="ent-msg-fuzz-") as root:
        source = Path(root) / "case.msg"
        for index in range(args.iterations):
            value = seeds[index % len(seeds)]
            if index % 5:
                for _ in range(rng.randrange(1, 8)):
                    offset = rng.randrange(len(value) + 1)
                    mutation = rng.choice(alphabet)
                    if rng.randrange(3):
                        value = value[:offset] + mutation + value[offset:]
                    else:
                        value = value[:offset] + value[offset + 1:]
            source.write_text(value, encoding="utf-8")
            try:
                rows, modules, submodules = generator._parse_specs([source], "ENT")
            except ValueError:
                rejected += 1
                continue
            except Exception:
                # Preserve the reproducer outside the temporary directory.
                Path("msg-fuzz-failure.msg").write_text(value, encoding="utf-8")
                raise
            accepted += 1
            try:
                codes = [row["raw_code"] for row in rows]
                assert len(codes) == len(set(codes)), value
                assert all(0 <= code <= 4294967295 for code in codes), value
                assert all(0 <= key <= 255 for key in modules), value
                assert all(0 <= key[1] <= 255 for key in submodules), value
                # Exercise the emitter's untrusted-text boundary as well.
                escaped = generator._as_c_string(value)
                assert all(ord(char) >= 32 for char in escaped), repr(value)
            except Exception:
                Path("msg-fuzz-failure.msg").write_text(value, encoding="utf-8")
                raise
    assert accepted and (rejected or args.iterations < 5)
    report = {"seed": args.seed, "iterations": args.iterations,
              "accepted": accepted, "rejected": rejected}
    print(json.dumps(report))
    if args.output:
        args.output.write_text(json.dumps(report, indent=2) + "\n")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
