#!/usr/bin/env python3
"""Preflight: lay every design sheet over the others and list what is not finished.

Each sheet is design/sheets/<name>.json with "columns" and "rows". Every row x column
crossing is a checkbox:
  BLOCKING  - cell missing/empty, wrong type, a reference that does not resolve,
              a duplicate id/unique value, a hook whose file lacks the method.
  OPEN      - cell marked in the row's "_unverified" map (needs checking in the
              running game or on a PC with the games installed).
  PLANNED   - rows with "status": "planned" (not built yet, not generated).

Exit code 1 when anything is BLOCKING. With --release, OPEN cells block too.
"""
import argparse
import json
import os
import re
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SHEETS = os.path.join(ROOT, "design", "sheets")
SCRIPTS = os.path.join(ROOT, "mod", "RivalsDayZ", "scripts")

NA = "n/a"


def load_sheets():
    sheets = {}
    for name in sorted(os.listdir(SHEETS)):
        if name.endswith(".json"):
            with open(os.path.join(SHEETS, name), encoding="utf-8") as f:
                data = json.load(f)
            sheets[data["sheet"]] = data
    return sheets


def is_empty(v):
    return v is None or (isinstance(v, str) and v.strip() in ("", "TODO", "?"))


def check_scalar(kind, value):
    if kind == "string":
        return isinstance(value, str)
    if kind == "number":
        return isinstance(value, (int, float)) and not isinstance(value, bool)
    if kind == "int":
        return isinstance(value, int) and not isinstance(value, bool)
    if kind == "bool":
        return isinstance(value, bool)
    if kind == "scalar":
        return isinstance(value, (str, int, float)) and not isinstance(value, bool)
    if kind == "id":
        return isinstance(value, str) and re.fullmatch(r"[A-Za-z_][A-Za-z0-9_]*", value) is not None
    if kind.startswith("enum:"):
        return value in kind[5:].split("|")
    return True


def preflight(vanilla=None):
    sheets = load_sheets()
    blocking, open_checks, planned = [], [], []
    ids = {name: {row.get("id") for row in s["rows"]} for name, s in sheets.items()}

    for name, sheet in sheets.items():
        cols = sheet["columns"]
        seen_unique = {c: {} for c, spec in cols.items() if spec.get("unique") or spec["type"] == "id"}
        for idx, row in enumerate(sheet["rows"]):
            rid = row.get("id", f"#{idx}")
            where = f"{name}.{rid}"
            if row.get("status") == "planned":
                planned.append(f"{where}: {row.get('doc', '')}")
                continue
            unknown = [k for k in row if not k.startswith("_") and k not in cols]
            for k in unknown:
                blocking.append(f"{where}.{k}: column not declared in sheet")
            for col, spec in cols.items():
                kind = spec["type"]
                cell = f"{where}.{col}"
                if col not in row or is_empty(row[col]):
                    blocking.append(f"{cell}: unfilled")
                    continue
                value = row[col]
                if value == NA:
                    if not spec.get("allow_na"):
                        blocking.append(f"{cell}: 'n/a' not allowed here")
                    continue
                if kind.startswith("list:"):
                    inner = kind[5:]
                    if not isinstance(value, list):
                        blocking.append(f"{cell}: expected a list")
                        continue
                    if not value and not spec.get("allow_empty"):
                        blocking.append(f"{cell}: empty list")
                    for item in value:
                        if inner.startswith("ref:"):
                            target = inner[4:]
                            if item not in ids.get(target, set()):
                                blocking.append(f"{cell}: '{item}' does not resolve in sheet '{target}'")
                        elif not check_scalar(inner, item):
                            blocking.append(f"{cell}: '{item}' is not {inner}")
                    continue
                if kind.startswith("ref:"):
                    target = kind[4:]
                    if target not in sheets:
                        blocking.append(f"{cell}: referenced sheet '{target}' does not exist")
                    elif value not in ids[target]:
                        blocking.append(f"{cell}: '{value}' does not resolve in sheet '{target}'")
                    continue
                if not check_scalar(kind, value):
                    blocking.append(f"{cell}: '{value}' is not {kind}")
                    continue
                if col in seen_unique:
                    if value in seen_unique[col]:
                        blocking.append(f"{cell}: duplicate of {seen_unique[col][value]}")
                    else:
                        seen_unique[col][value] = rid
            for col, why in row.get("_unverified", {}).items():
                if col not in cols:
                    blocking.append(f"{where}._unverified.{col}: no such column")
                else:
                    open_checks.append(f"{where}.{col}: {why}")

    # Hooks: the named script file must contain the modded class and the method.
    for row in sheets.get("hooks", {}).get("rows", []):
        path = os.path.join(SCRIPTS, row["file"])
        where = f"hooks.{row['id']}"
        if not os.path.isfile(path):
            blocking.append(f"{where}.file: {row['file']} does not exist")
            continue
        with open(path, encoding="utf-8") as f:
            src = f.read()
        if row.get("hook_kind") == "subclass":
            if not re.search(r"class\s+\w+\s+(extends|:)\s+" + re.escape(row["dayz_class"]) + r"\b", src):
                blocking.append(f"{where}: {row['file']} has no class extending {row['dayz_class']}")
        elif not re.search(r"modded\s+class\s+" + re.escape(row["dayz_class"]) + r"\b", src):
            blocking.append(f"{where}: {row['file']} has no 'modded class {row['dayz_class']}'")
        if not re.search(r"\boverride\b[^;{]*\b" + re.escape(row["method"]) + r"\s*\(", src):
            blocking.append(f"{where}: {row['file']} does not override {row['method']}")
        if vanilla:
            vfile = row["vanilla_ref"].split(" ")[0]
            vpath = os.path.join(vanilla, vfile)
            if not os.path.isfile(vpath):
                blocking.append(f"{where}.vanilla_ref: {vfile} not found in DayZ scripts")
            else:
                with open(vpath, encoding="utf-8", errors="replace") as f:
                    if not re.search(r"\b" + re.escape(row["method"]) + r"\s*\(", f.read()):
                        blocking.append(f"{where}.vanilla_ref: {row['method']} not found in {vfile}")

    # Cross-sheet rules that types alone do not catch.
    abilities = {r["id"]: r for r in sheets["abilities"]["rows"]}
    for item in sheets["items"]["rows"]:
        for ab in item["grants"]:
            if ab in abilities and abilities[ab]["hero"] != item["hero"]:
                blocking.append(f"items.{item['id']}.grants: {ab} belongs to {abilities[ab]['hero']}, not {item['hero']}")
    for hero in sheets["heroes"]["rows"]:
        granted = {a for it in sheets["items"]["rows"] if it["id"] == hero["power_item"] for a in it["grants"]}
        own = {a for a, r in abilities.items() if r["hero"] == hero["id"]}
        for missing in sorted(own - granted):
            blocking.append(f"heroes.{hero['id']}: ability {missing} is not granted by {hero['power_item']}")
    starts = [r["id"] for r in sheets["spawn_points"]["rows"] if r.get("role") == "player_start"]
    if len(starts) != 1:
        blocking.append(f"spawn_points: exactly one row must have role player_start (found {len(starts)})")
    used_inputs = {r["input"] for r in abilities.values()}
    for inp in sheets["inputs"]["rows"]:
        if inp["kind"] == "mod" and inp["id"] not in used_inputs and inp["id"] != "in_spawn_menu":
            blocking.append(f"inputs.{inp['id']}: declared but no ability uses it")
    return blocking, open_checks, planned


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--release", action="store_true", help="treat open (unverified) cells as blocking")
    ap.add_argument("--vanilla", help="path to DayZ-Script-Diff/scripts to check hooks against")
    args = ap.parse_args()
    blocking, open_checks, planned = preflight(args.vanilla)
    print(f"BLOCKING ({len(blocking)})")
    for b in blocking:
        print("  x " + b)
    print(f"OPEN - needs checking in game or on the PC ({len(open_checks)})")
    for o in open_checks:
        print("  ? " + o)
    print(f"PLANNED - not built yet ({len(planned)})")
    for p in planned:
        print("  - " + p)
    if blocking or (args.release and open_checks):
        sys.exit(1)


if __name__ == "__main__":
    main()
