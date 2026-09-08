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
import re
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
    symbols: Dict[str, str] = {}
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

        if module_name is None or submodule_name is None or len(parts) < 4:
            raise ValueError(
                f"{source}:{line_no}: message requires module and submodule"
            )

        message_name = parts[0].upper()
        old_symbol = gen_ent_msg._symbol_name(
            symbol_prefix, module_name, submodule_name, message_name
        )
        if explicit_prefix is None:
            final_symbol = old_symbol
        else:
            final_symbol = f"{explicit_prefix}_{message_name}"

        previous = symbols.get(old_symbol)
        if previous is not None and previous != final_symbol:
            raise ValueError(
                f"{source}:{line_no}: conflicting replacement for '{old_symbol}'"
            )
        symbols[old_symbol] = final_symbol
        transformed.append(raw_line)

    output.write_text("\n".join(transformed) + "\n", encoding="utf-8")
    return symbols


def _rewrite_generated(
    path: Path,
    symbols: Dict[str, str],
    temporary_inputs: Sequence[Path],
    source_inputs: Sequence[Path],
) -> None:
    text = path.read_text(encoding="utf-8")
    changed_symbols = {
        old_symbol: final_symbol
        for old_symbol, final_symbol in symbols.items()
        if old_symbol != final_symbol
    }
    if changed_symbols:
        alternatives = "|".join(
            re.escape(symbol)
            for symbol in sorted(changed_symbols, key=len, reverse=True)
        )
        token_pattern = re.compile(
            rf"(?<![A-Za-z0-9_])(?:{alternatives})(?![A-Za-z0-9_])"
        )
        text = token_pattern.sub(
            lambda match: changed_symbols[match.group(0)],
            text,
        )

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
    symbols: Dict[str, str] = {}
    final_symbol_owners: Dict[str, str] = {}
    source_inputs = [Path(raw_path) for raw_path in known.input]

    with tempfile.TemporaryDirectory() as temp_dir:
        temp_root = Path(temp_dir)
        transformed_inputs = []
        for index, source in enumerate(source_inputs):
            transformed = temp_root / f"spec-{index}.msg"
            current = _parse_spec(source, transformed, symbol_prefix)
            for old_symbol, final_symbol in current.items():
                previous = symbols.get(old_symbol)
                if previous is not None and previous != final_symbol:
                    raise ValueError(
                        f"conflicting replacement for '{old_symbol}'"
                    )
                previous_owner = final_symbol_owners.get(final_symbol)
                if previous_owner is not None and previous_owner != old_symbol:
                    raise ValueError(
                        f"conflicting generated symbol '{final_symbol}' from "
                        f"'{previous_owner}' and '{old_symbol}'"
                    )
                symbols[old_symbol] = final_symbol
                final_symbol_owners[final_symbol] = old_symbol
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
        if result != 0:
            return result

        _rewrite_generated(
            Path(known.header),
            symbols,
            transformed_inputs,
            source_inputs,
        )
        _rewrite_generated(
            Path(known.source),
            symbols,
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
