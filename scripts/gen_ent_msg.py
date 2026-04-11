#!/usr/bin/env python3
import argparse
import re
import sys
from pathlib import Path

ENT_MSG_SIGN_MASK = 0x80000000
ENT_MSG_MODULE_SHIFT = 23
ENT_MSG_SUBMODULE_SHIFT = 15
ENT_MSG_MODULE_MASK = 0xFF
ENT_MSG_SUBMODULE_MASK = 0xFF
ENT_MSG_INNER_MASK = 0x7FFF
MAX_GROUP_NAME_LEN = 4

NAME_RE = re.compile(r"^[A-Z][A-Z0-9_]*$")


def _as_c_string(text: str) -> str:
    escaped = text.replace("\\", "\\\\").replace('"', '\\"')
    return f"\"{escaped}\""


def _parse_int(raw: str, field: str, line_no: int) -> int:
    try:
        return int(raw, 0)
    except ValueError as exc:
        raise ValueError(f"line {line_no}: invalid {field} '{raw}'") from exc


def _encode_code(severity: str, module: int, submodule: int, inner: int) -> int:
    raw = (
        ((module & ENT_MSG_MODULE_MASK) << ENT_MSG_MODULE_SHIFT)
        | ((submodule & ENT_MSG_SUBMODULE_MASK) << ENT_MSG_SUBMODULE_SHIFT)
        | (inner & ENT_MSG_INNER_MASK)
    )
    if severity == "err":
        raw |= ENT_MSG_SIGN_MASK
    return raw


def _validate_name(name: str, field: str, line_no: int):
    if not NAME_RE.match(name):
        raise ValueError(
            f"line {line_no}: {field} '{name}' must match {NAME_RE.pattern}"
        )
    if field in ("module name", "submodule name") and len(name) > MAX_GROUP_NAME_LEN:
        raise ValueError(
            f"line {line_no}: {field} '{name}' length must be <= {MAX_GROUP_NAME_LEN}"
        )


def _parse_msg(path: Path):
    rows = []
    module_names = {}
    submodule_names = {}
    seen_symbol = set()
    seen_code = {}

    module_name = None
    module_id = None
    current_submodule_name = None
    current_submodule_id = None

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
                        f"line {line_no}: module line must be 'module <name> <id>'"
                    )
                if module_name is not None:
                    raise ValueError(f"line {line_no}: module is already defined")
                module_name = parts[1].upper()
                _validate_name(module_name, "module name", line_no)
                module_id = _parse_int(parts[2], "module id", line_no)
                if not (0 <= module_id <= ENT_MSG_MODULE_MASK):
                    raise ValueError(f"line {line_no}: module id must be 0..255")
                module_names[module_id] = module_name
                continue

            if key == "submodule":
                if len(parts) != 3:
                    raise ValueError(
                        f"line {line_no}: submodule line must be 'submodule <name> <id>'"
                    )
                if module_name is None:
                    raise ValueError(
                        f"line {line_no}: module must be defined before submodule"
                    )
                current_submodule_name = parts[1].upper()
                _validate_name(current_submodule_name, "submodule name", line_no)
                current_submodule_id = _parse_int(parts[2], "submodule id", line_no)
                if not (0 <= current_submodule_id <= ENT_MSG_SUBMODULE_MASK):
                    raise ValueError(f"line {line_no}: submodule id must be 0..255")
                sub_key = (module_id, current_submodule_id)
                if sub_key in submodule_names and submodule_names[sub_key] != current_submodule_name:
                    raise ValueError(
                        f"line {line_no}: conflicting submodule name for id {current_submodule_id}"
                    )
                submodule_names[sub_key] = current_submodule_name
                continue

            if module_name is None:
                raise ValueError(f"line {line_no}: module must be defined before messages")
            if current_submodule_name is None:
                raise ValueError(f"line {line_no}: submodule must be defined before messages")
            if len(parts) < 4:
                raise ValueError(
                    f"line {line_no}: message line must be '<name> <ok|err> <inner> <message>'"
                )

            msg_name = parts[0].upper()
            _validate_name(msg_name, "message name", line_no)
            severity = parts[1].lower()
            if severity not in ("ok", "err"):
                raise ValueError(
                    f"line {line_no}: severity must be 'ok' or 'err', got '{parts[1]}'"
                )
            inner = _parse_int(parts[2], "inner id", line_no)
            if not (0 <= inner <= ENT_MSG_INNER_MASK):
                raise ValueError(f"line {line_no}: inner id must be 0..32767")

            message = " ".join(parts[3:]).strip()
            if not message:
                raise ValueError(f"line {line_no}: message text is empty")

            symbol = f"{module_name}_{current_submodule_name}_{msg_name}"
            if symbol in seen_symbol:
                raise ValueError(f"line {line_no}: duplicate symbol '{symbol}'")
            if len(symbol.split("_")) > 4:
                raise ValueError(
                    f"line {line_no}: symbol '{symbol}' has more than 4 segments"
                )
            seen_symbol.add(symbol)

            code_key = (severity, module_id, current_submodule_id, inner)
            if code_key in seen_code:
                raise ValueError(
                    f"line {line_no}: duplicate code tuple already used by '{seen_code[code_key]}'"
                )
            seen_code[code_key] = symbol

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
                    "raw_code": _encode_code(severity, module_id, current_submodule_id, inner),
                }
            )

    if module_name is None:
        raise ValueError("module is not defined")
    if not rows:
        raise ValueError("no messages defined")
    return rows, module_names, submodule_names


def _write_header(path: Path, rows):
    lines = []
    lines.append("#ifndef _ENT_MSG_GEN_H_")
    lines.append("#define _ENT_MSG_GEN_H_")
    lines.append("")
    lines.append("#include <stddef.h>")
    lines.append('#include "ent_types.h"')
    lines.append("")
    lines.append("typedef struct")
    lines.append("{")
    lines.append("    MSG_ID_T code;")
    lines.append("    unsigned char module;")
    lines.append("    unsigned char submodule;")
    lines.append("    const char* moduleName;")
    lines.append("    const char* submoduleName;")
    lines.append("    const char* message;")
    lines.append("} ENT_MSG_ITEM_T;")
    lines.append("")
    lines.append("typedef struct")
    lines.append("{")
    lines.append("    unsigned char module;")
    lines.append("    const char* moduleName;")
    lines.append("} ENT_MSG_MODULE_ITEM_T;")
    lines.append("")
    lines.append("typedef struct")
    lines.append("{")
    lines.append("    unsigned char module;")
    lines.append("    unsigned char submodule;")
    lines.append("    const char* submoduleName;")
    lines.append("} ENT_MSG_SUBMODULE_ITEM_T;")
    lines.append("")
    lines.append("extern const ENT_MSG_ITEM_T g_ent_msg_items[];")
    lines.append("extern const size_t g_ent_msg_items_count;")
    lines.append("extern const ENT_MSG_MODULE_ITEM_T g_ent_msg_modules[];")
    lines.append("extern const size_t g_ent_msg_modules_count;")
    lines.append("extern const ENT_MSG_SUBMODULE_ITEM_T g_ent_msg_submodules[];")
    lines.append("extern const size_t g_ent_msg_submodules_count;")
    lines.append("")
    for row in rows:
        lines.append(f"#define {row['symbol']} ((MSG_ID_T)0x{row['raw_code']:08X}u)")
    lines.append("")
    lines.append("#endif")
    lines.append("")
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text("\n".join(lines), encoding="utf-8")


def _write_source(path: Path, rows, module_names, submodule_names):
    module_items = sorted(module_names.items(), key=lambda x: x[0])
    submodule_items = sorted(submodule_names.items(), key=lambda x: (x[0][0], x[0][1]))

    lines = []
    lines.append('#include "ent_msg_gen.h"')
    lines.append("")
    lines.append("const ENT_MSG_ITEM_T g_ent_msg_items[] =")
    lines.append("{")
    for row in rows:
        lines.append(
            "    {" +
            f"{row['symbol']}, {row['module']}u, {row['submodule']}u, "
            f"{_as_c_string(row['module_name'])}, "
            f"{_as_c_string(row['submodule_name'])}, "
            f"{_as_c_string(row['message'])}" +
            "},"
        )
    lines.append("};")
    lines.append("")
    lines.append(
        "const size_t g_ent_msg_items_count = sizeof(g_ent_msg_items) / sizeof(g_ent_msg_items[0]);"
    )
    lines.append("")
    lines.append("const ENT_MSG_MODULE_ITEM_T g_ent_msg_modules[] =")
    lines.append("{")
    for module, module_name in module_items:
        lines.append(f"    {{{module}u, {_as_c_string(module_name)}}},")
    lines.append("};")
    lines.append("")
    lines.append(
        "const size_t g_ent_msg_modules_count = sizeof(g_ent_msg_modules) / sizeof(g_ent_msg_modules[0]);"
    )
    lines.append("")
    lines.append("const ENT_MSG_SUBMODULE_ITEM_T g_ent_msg_submodules[] =")
    lines.append("{")
    for (module, submodule), submodule_name in submodule_items:
        lines.append(
            f"    {{{module}u, {submodule}u, {_as_c_string(submodule_name)}}},"
        )
    lines.append("};")
    lines.append("")
    lines.append(
        "const size_t g_ent_msg_submodules_count = sizeof(g_ent_msg_submodules) / sizeof(g_ent_msg_submodules[0]);"
    )
    lines.append("")
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text("\n".join(lines), encoding="utf-8")


def main():
    parser = argparse.ArgumentParser(description="Generate ENT message code files from .msg specification.")
    parser.add_argument("--input", required=True, help="Input .msg file.")
    parser.add_argument("--header", required=True, help="Generated header output path.")
    parser.add_argument("--source", required=True, help="Generated source output path.")
    args = parser.parse_args()

    input_path = Path(args.input)
    header_path = Path(args.header)
    source_path = Path(args.source)

    rows, module_names, submodule_names = _parse_msg(input_path)
    _write_header(header_path, rows)
    _write_source(source_path, rows, module_names, submodule_names)
    return 0


if __name__ == "__main__":
    try:
        sys.exit(main())
    except Exception as exc:  # pragma: no cover
        print(f"gen_ent_msg.py error: {exc}", file=sys.stderr)
        sys.exit(1)
