#!/usr/bin/env python3
"""Generate C message catalog sources from one or more .msg specifications."""

import argparse
import re
import sys
from pathlib import Path
from typing import Dict, List, Optional, Sequence, Tuple

GENERATOR_VERSION = 2

ENT_MSG_SIGN_MASK = 0x80000000
ENT_MSG_MODULE_SHIFT = 23
ENT_MSG_SUBMODULE_SHIFT = 15
ENT_MSG_MODULE_MASK = 0xFF
ENT_MSG_SUBMODULE_MASK = 0xFF
ENT_MSG_INNER_MASK = 0x7FFF
MAX_GROUP_NAME_LEN = 4

NAME_RE = re.compile(r"^[A-Z][A-Z0-9_]*$")
PREFIX_RE = re.compile(r"^[A-Za-z_][A-Za-z0-9_]*$")


def _as_c_string(text: str) -> str:
    escaped = text.replace("\\", "\\\\").replace('"', '\\"')
    return f'"{escaped}"'


def _parse_int(raw: str, field: str, source: Path, line_no: int) -> int:
    try:
        return int(raw, 0)
    except ValueError as exc:
        raise ValueError(
            f"{source}:{line_no}: invalid {field} '{raw}'"
        ) from exc


def _encode_code(severity: str, module: int, submodule: int, inner: int) -> int:
    raw = (
        ((module & ENT_MSG_MODULE_MASK) << ENT_MSG_MODULE_SHIFT)
        | ((submodule & ENT_MSG_SUBMODULE_MASK) << ENT_MSG_SUBMODULE_SHIFT)
        | (inner & ENT_MSG_INNER_MASK)
    )
    if severity == "err":
        raw |= ENT_MSG_SIGN_MASK
    return raw


def _validate_name(name: str, field: str, source: Path, line_no: int) -> None:
    if not NAME_RE.match(name):
        raise ValueError(
            f"{source}:{line_no}: {field} '{name}' must match {NAME_RE.pattern}"
        )
    if field in ("module name", "submodule name") and len(name) > MAX_GROUP_NAME_LEN:
        raise ValueError(
            f"{source}:{line_no}: {field} '{name}' length must be "
            f"<= {MAX_GROUP_NAME_LEN}"
        )


def _validate_prefix(value: str, field: str) -> str:
    if not PREFIX_RE.match(value):
        raise ValueError(f"{field} '{value}' must be a valid C identifier prefix")
    return value


def _symbol_name(prefix: str, module_name: str, submodule_name: str, msg_name: str) -> str:
    if module_name == prefix.upper():
        return f"{prefix.upper()}_{submodule_name}_{msg_name}"
    return f"{prefix.upper()}_{module_name}_{submodule_name}_{msg_name}"


def _parse_specs(paths: Sequence[Path], symbol_prefix: str):
    rows: List[dict] = []
    module_names: Dict[int, str] = {}
    module_ids: Dict[str, int] = {}
    submodule_names: Dict[Tuple[int, int], str] = {}
    submodule_ids: Dict[Tuple[int, str], int] = {}
    seen_symbol = set()
    seen_raw_code: Dict[int, str] = {}

    for path in paths:
        module_name: Optional[str] = None
        module_id: Optional[int] = None
        current_submodule_name: Optional[str] = None
        current_submodule_id: Optional[int] = None

        with path.open("r", encoding="utf-8") as fp:
            for line_no, raw_line in enumerate(fp, start=1):
                line = raw_line.split("#", 1)[0].strip()
                if not line:
                    continue

                parts = line.split()
                key = parts[0].lower()

                if key == "module":
                    if len(parts) != 3:
                        raise ValueError(
                            f"{path}:{line_no}: module line must be "
                            "'module <name> <id>'"
                        )
                    candidate_name = parts[1].upper()
                    _validate_name(candidate_name, "module name", path, line_no)
                    candidate_id = _parse_int(parts[2], "module id", path, line_no)
                    if not (0 <= candidate_id <= ENT_MSG_MODULE_MASK):
                        raise ValueError(
                            f"{path}:{line_no}: module id must be 0..255"
                        )
                    existing_name = module_names.get(candidate_id)
                    if existing_name is not None and existing_name != candidate_name:
                        raise ValueError(
                            f"{path}:{line_no}: module id {candidate_id} already "
                            f"belongs to '{existing_name}'"
                        )
                    existing_id = module_ids.get(candidate_name)
                    if existing_id is not None and existing_id != candidate_id:
                        raise ValueError(
                            f"{path}:{line_no}: module '{candidate_name}' already "
                            f"uses id {existing_id}"
                        )
                    module_names[candidate_id] = candidate_name
                    module_ids[candidate_name] = candidate_id
                    module_name = candidate_name
                    module_id = candidate_id
                    current_submodule_name = None
                    current_submodule_id = None
                    continue

                if key == "submodule":
                    if len(parts) != 3:
                        raise ValueError(
                            f"{path}:{line_no}: submodule line must be "
                            "'submodule <name> <id>'"
                        )
                    if module_name is None or module_id is None:
                        raise ValueError(
                            f"{path}:{line_no}: module must be defined before submodule"
                        )
                    candidate_name = parts[1].upper()
                    _validate_name(candidate_name, "submodule name", path, line_no)
                    candidate_id = _parse_int(
                        parts[2], "submodule id", path, line_no
                    )
                    if not (0 <= candidate_id <= ENT_MSG_SUBMODULE_MASK):
                        raise ValueError(
                            f"{path}:{line_no}: submodule id must be 0..255"
                        )
                    key_by_id = (module_id, candidate_id)
                    key_by_name = (module_id, candidate_name)
                    existing_name = submodule_names.get(key_by_id)
                    if existing_name is not None and existing_name != candidate_name:
                        raise ValueError(
                            f"{path}:{line_no}: module '{module_name}' submodule id "
                            f"{candidate_id} already belongs to '{existing_name}'"
                        )
                    existing_id = submodule_ids.get(key_by_name)
                    if existing_id is not None and existing_id != candidate_id:
                        raise ValueError(
                            f"{path}:{line_no}: module '{module_name}' submodule "
                            f"'{candidate_name}' already uses id {existing_id}"
                        )
                    submodule_names[key_by_id] = candidate_name
                    submodule_ids[key_by_name] = candidate_id
                    current_submodule_name = candidate_name
                    current_submodule_id = candidate_id
                    continue

                if module_name is None or module_id is None:
                    raise ValueError(
                        f"{path}:{line_no}: module must be defined before messages"
                    )
                if current_submodule_name is None or current_submodule_id is None:
                    raise ValueError(
                        f"{path}:{line_no}: submodule must be defined before messages"
                    )
                if len(parts) < 4:
                    raise ValueError(
                        f"{path}:{line_no}: message line must be "
                        "'<name> <ok|err> <inner> <message>'"
                    )

                msg_name = parts[0].upper()
                _validate_name(msg_name, "message name", path, line_no)
                severity = parts[1].lower()
                if severity not in ("ok", "err"):
                    raise ValueError(
                        f"{path}:{line_no}: severity must be 'ok' or 'err', "
                        f"got '{parts[1]}'"
                    )
                inner = _parse_int(parts[2], "inner id", path, line_no)
                if not (0 <= inner <= ENT_MSG_INNER_MASK):
                    raise ValueError(
                        f"{path}:{line_no}: inner id must be 0..32767"
                    )

                message = " ".join(parts[3:]).strip()
                if not message:
                    raise ValueError(f"{path}:{line_no}: message text is empty")

                symbol = _symbol_name(
                    symbol_prefix, module_name, current_submodule_name, msg_name
                )
                if symbol in seen_symbol:
                    raise ValueError(
                        f"{path}:{line_no}: duplicate symbol '{symbol}'"
                    )

                raw_code = _encode_code(
                    severity, module_id, current_submodule_id, inner
                )
                if raw_code in seen_raw_code:
                    raise ValueError(
                        f"{path}:{line_no}: duplicate encoded value "
                        f"0x{raw_code:08X} already used by "
                        f"'{seen_raw_code[raw_code]}'"
                    )

                seen_symbol.add(symbol)
                seen_raw_code[raw_code] = symbol
                rows.append(
                    {
                        "symbol": symbol,
                        "severity": severity,
                        "module": module_id,
                        "module_name": module_name,
                        "submodule": current_submodule_id,
                        "submodule_name": current_submodule_name,
                        "inner": inner,
                        "message": message,
                        "raw_code": raw_code,
                    }
                )

    if not module_names:
        raise ValueError("module is not defined")
    if not rows:
        raise ValueError("no messages defined")
    return rows, module_names, submodule_names


def _generated_notice(inputs: Sequence[Path]) -> List[str]:
    sources = ", ".join(path.name for path in inputs)
    return [
        "/* GENERATED FILE - DO NOT EDIT.",
        f" * Generator: gen_ent_msg.py v{GENERATOR_VERSION}",
        f" * Sources: {sources}",
        " */",
    ]


def _write_header(
    path: Path,
    rows,
    inputs: Sequence[Path],
    include_guard: str,
    type_prefix: str,
    table_prefix: str,
    types_header: str,
    emit_symbol_name: bool,
) -> None:
    item_type = f"{type_prefix}_MSG_ITEM_T"
    module_type = f"{type_prefix}_MSG_MODULE_ITEM_T"
    submodule_type = f"{type_prefix}_MSG_SUBMODULE_ITEM_T"

    lines = _generated_notice(inputs)
    lines.extend(
        [
            f"#ifndef {include_guard}",
            f"#define {include_guard}",
            "",
            "#include <stddef.h>",
            f'#include "{types_header}"',
            "",
            "typedef struct",
            "{",
            "    MSG_ID_T code;",
            "    unsigned char module;",
            "    unsigned char submodule;",
            "    const char* moduleName;",
            "    const char* submoduleName;",
        ]
    )
    if emit_symbol_name:
        lines.append("    const char* symbol;")
    lines.extend(
        [
            "    const char* message;",
            f"}} {item_type};",
            "",
            "typedef struct",
            "{",
            "    unsigned char module;",
            "    const char* moduleName;",
            f"}} {module_type};",
            "",
            "typedef struct",
            "{",
            "    unsigned char module;",
            "    unsigned char submodule;",
            "    const char* submoduleName;",
            f"}} {submodule_type};",
            "",
            f"extern const {item_type} {table_prefix}_items[];",
            f"extern const size_t {table_prefix}_items_count;",
            f"extern const {module_type} {table_prefix}_modules[];",
            f"extern const size_t {table_prefix}_modules_count;",
            f"extern const {submodule_type} {table_prefix}_submodules[];",
            f"extern const size_t {table_prefix}_submodules_count;",
            "",
        ]
    )
    for row in rows:
        lines.append(
            f"#define {row['symbol']} ((MSG_ID_T)0x{row['raw_code']:08X}u)"
        )
    lines.extend(["", "#endif", ""])
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text("\n".join(lines), encoding="utf-8")


def _write_source(
    path: Path,
    rows,
    module_names,
    submodule_names,
    inputs: Sequence[Path],
    header_name: str,
    type_prefix: str,
    table_prefix: str,
    emit_symbol_name: bool,
) -> None:
    item_type = f"{type_prefix}_MSG_ITEM_T"
    module_type = f"{type_prefix}_MSG_MODULE_ITEM_T"
    submodule_type = f"{type_prefix}_MSG_SUBMODULE_ITEM_T"
    module_items = sorted(module_names.items(), key=lambda item: item[0])
    submodule_items = sorted(
        submodule_names.items(), key=lambda item: (item[0][0], item[0][1])
    )

    lines = _generated_notice(inputs)
    lines.extend(
        [
            f'#include "{header_name}"',
            "",
            f"const {item_type} {table_prefix}_items[] =",
            "{",
        ]
    )
    for row in rows:
        fields = [
            row["symbol"],
            f"{row['module']}u",
            f"{row['submodule']}u",
            _as_c_string(row["module_name"]),
            _as_c_string(row["submodule_name"]),
        ]
        if emit_symbol_name:
            fields.append(_as_c_string(row["symbol"]))
        fields.append(_as_c_string(row["message"]))
        lines.append("    {" + ", ".join(fields) + "},")
    lines.extend(
        [
            "};",
            "",
            f"const size_t {table_prefix}_items_count = "
            f"sizeof({table_prefix}_items) / sizeof({table_prefix}_items[0]);",
            "",
            f"const {module_type} {table_prefix}_modules[] =",
            "{",
        ]
    )
    for module, module_name in module_items:
        lines.append(f"    {{{module}u, {_as_c_string(module_name)}}},")
    lines.extend(
        [
            "};",
            "",
            f"const size_t {table_prefix}_modules_count = "
            f"sizeof({table_prefix}_modules) / sizeof({table_prefix}_modules[0]);",
            "",
            f"const {submodule_type} {table_prefix}_submodules[] =",
            "{",
        ]
    )
    for (module, submodule), submodule_name in submodule_items:
        lines.append(
            f"    {{{module}u, {submodule}u, {_as_c_string(submodule_name)}}},"
        )
    lines.extend(
        [
            "};",
            "",
            f"const size_t {table_prefix}_submodules_count = "
            f"sizeof({table_prefix}_submodules) / "
            f"sizeof({table_prefix}_submodules[0]);",
            "",
        ]
    )
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text("\n".join(lines), encoding="utf-8")


def main(argv: Optional[Sequence[str]] = None) -> int:
    parser = argparse.ArgumentParser(
        description="Generate C message catalog files from .msg specifications."
    )
    parser.add_argument(
        "--input",
        action="append",
        required=True,
        help="Input .msg file. Repeat for multiple specifications.",
    )
    parser.add_argument("--header", required=True, help="Generated header output path.")
    parser.add_argument("--source", required=True, help="Generated source output path.")
    parser.add_argument("--symbol-prefix", default="ENT")
    parser.add_argument("--type-prefix", default="ENT")
    parser.add_argument("--table-prefix", default="g_ent_msg")
    parser.add_argument("--include-guard", default="_ENT_MSG_GEN_H_")
    parser.add_argument("--types-header", default="ent_types.h")
    parser.add_argument(
        "--source-header",
        default=None,
        help="Header spelling used by the generated source; defaults to output basename.",
    )
    parser.add_argument(
        "--emit-symbol-name",
        action="store_true",
        help="Include the symbolic status name in each generated catalog item.",
    )
    args = parser.parse_args(argv)

    symbol_prefix = _validate_prefix(args.symbol_prefix, "symbol prefix").upper()
    type_prefix = _validate_prefix(args.type_prefix, "type prefix").upper()
    table_prefix = _validate_prefix(args.table_prefix, "table prefix")
    include_guard = _validate_prefix(args.include_guard, "include guard").upper()

    input_paths = [Path(raw) for raw in args.input]
    for input_path in input_paths:
        if not input_path.is_file():
            raise ValueError(f"input file does not exist: {input_path}")

    header_path = Path(args.header)
    source_path = Path(args.source)
    source_header = args.source_header or header_path.name

    rows, module_names, submodule_names = _parse_specs(
        input_paths, symbol_prefix
    )
    _write_header(
        header_path,
        rows,
        input_paths,
        include_guard,
        type_prefix,
        table_prefix,
        args.types_header,
        args.emit_symbol_name,
    )
    _write_source(
        source_path,
        rows,
        module_names,
        submodule_names,
        input_paths,
        source_header,
        type_prefix,
        table_prefix,
        args.emit_symbol_name,
    )
    return 0


if __name__ == "__main__":
    try:
        sys.exit(main())
    except Exception as exc:  # pragma: no cover
        print(f"gen_ent_msg.py error: {exc}", file=sys.stderr)
        sys.exit(1)
