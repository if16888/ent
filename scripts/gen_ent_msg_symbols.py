#!/usr/bin/env python3
"""Generate a catalog while allowing stable per-submodule symbol prefixes.

The base `.msg` grammar remains unchanged. A submodule may optionally use:

    submodule <NAME> <ID> symbol <C_PREFIX>

The wrapper removes the optional suffix before invoking `gen_ent_msg.py`, then
rewrites only the generated status symbols to the requested canonical prefix.
All numeric-code, duplicate-code, name, range, and message validation remains in
the base generator.
"""

import argparse
import sys
import tempfile
from pathlib import Path
from typing import Dict, List, Optional, Sequence

import gen_ent_msg


def _parse_spec(
    source: Path,
    output: Path,
    symbol_prefix: str,
) -> Dict[str, str]:
    replacements: Dict[str, str] = {}
    transformed: List[str] = []
    module_name: Optional[str] = None
    submodule_name: Optional[str] = None
    explicit_prefix: Optional[str] = None

    for line_no, raw_line in enumerate(
        source.read_text(encoding="utf-8").splitlines(), start=1
    ):
        code = raw_line.split("#", 1)[0].strip()
        if not code:
            transformed.append(raw_line)
            continue

        parts = code.split()
        keyword = parts[0].lower()
        if keyword == "module":
            if len(parts) == 3:
                module_name = parts[1].upper()
                submodule_name = None
                explicit_prefix = None
            transformed.append(raw_line)
            continue

        if keyword == "submodule":
            if len(parts) == 3:
                submodule_name = parts[1].upper()
                explicit_prefix = None
                transformed.append(raw_line)
                continue
            if len(parts) != 5 or parts[3].lower() != "symbol":
                raise ValueError(
                    f"{source}:{line_no}: submodule line must be "
                    "'submodule <name> <id>' or "
                    "'submodule <name> <id> symbol <prefix>'"
                )
            explicit_prefix = gen_ent_msg._validate_prefix(
                parts[4].upper(), "submodule symbol prefix"
            ).upper()
            submodule_name = parts[1].upper()
            comment = ""
            if "#" in raw_line:
                comment = " #" + raw_line.split("#", 1)[1]
            transformed.append(f"submodule {parts[1]} {parts[2]}{comment}")
            continue

        if explicit_prefix is not None:
            if module_name is None or submodule_name is None or len(parts) < 4:
                raise ValueError(
                    f"{source}:{line_no}: message requires module and submodule"
                )
            message_name = parts[0].upper()
            old_symbol = gen_ent_msg._symbol_name(
                symbol_prefix, module_name, submodule_name, message_name
            )
            new_symbol = f"{explicit_prefix}_{message_name}"
            old_replacement = replacements.get(old_symbol)
            if old_replacement is not None and old_replacement != new_symbol:
                raise ValueError(
                    f"{source}:{line_no}: conflicting replacement for '{old_symbol}'"
                )
            replacements[old_symbol] = new_symbol
        transformed.append(raw_line)

    output.write_text("\n".join(transformed) + "\n", encoding="utf-8")
    return replacements


def _rewrite_generated(
    path: Path,
    replacements: Dict[str, str],
    temporary_inputs: Sequence[Path],
    source_inputs: Sequence[Path],
) -> None:
    text = path.read_text(encoding="utf-8")
    for old_symbol in sorted(replacements, key=len, reverse=True):
        text = text.replace(old_symbol, replacements[old_symbol])

    temporary_notice = ", ".join(item.name for item in temporary_inputs)
    source_notice = ", ".join(item.name for item in source_inputs)
    text = text.replace(
        f" * Sources: {temporary_notice}",
        f" * Sources: {source_notice}",
        1,
    )
    path.write_text(text, encoding="utf-8")


def main(argv: Optional[Sequence[str]] = None) -> int:
    parser = argparse.ArgumentParser(add_help=False)
    parser.add_argument("--input", action="append", required=True)
    parser.add_argument("--header", required=True)
    parser.add_argument("--source", required=True)
    parser.add_argument("--symbol-prefix", default="ENT")
    known, remaining = parser.parse_known_args(argv)

    symbol_prefix = gen_ent_msg._validate_prefix(
        known.symbol_prefix, "symbol prefix"
    ).upper()
    replacements: Dict[str, str] = {}
    final_symbol_owners: Dict[str, str] = {}
    source_inputs = [Path(raw_path) for raw_path in known.input]

    with tempfile.TemporaryDirectory() as temp_dir:
        temp_root = Path(temp_dir)
        transformed_inputs = []
        for index, source in enumerate(source_inputs):
            transformed = temp_root / f"spec-{index}.msg"
            current = _parse_spec(source, transformed, symbol_prefix)
            for old_symbol, new_symbol in current.items():
                previous = replacements.get(old_symbol)
                if previous is not None and previous != new_symbol:
                    raise ValueError(
                        f"conflicting replacement for '{old_symbol}'"
                    )
                previous_owner = final_symbol_owners.get(new_symbol)
                if previous_owner is not None and previous_owner != old_symbol:
                    raise ValueError(
                        f"conflicting generated symbol '{new_symbol}' from "
                        f"'{previous_owner}' and '{old_symbol}'"
                    )
                replacements[old_symbol] = new_symbol
                final_symbol_owners[new_symbol] = old_symbol
            transformed_inputs.append(transformed)

        base_args: List[str] = []
        for transformed in transformed_inputs:
            base_args.extend(["--input", str(transformed)])
        base_args.extend(
            [
                "--header",
                known.header,
                "--source",
                known.source,
                "--symbol-prefix",
                symbol_prefix,
                *remaining,
            ]
        )
        result = gen_ent_msg.main(base_args)

        _rewrite_generated(
            Path(known.header),
            replacements,
            transformed_inputs,
            source_inputs,
        )
        _rewrite_generated(
            Path(known.source),
            replacements,
            transformed_inputs,
            source_inputs,
        )
    return result


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except Exception as exc:  # pragma: no cover
        print(f"gen_ent_msg_symbols.py error: {exc}", file=sys.stderr)
        raise SystemExit(1)
