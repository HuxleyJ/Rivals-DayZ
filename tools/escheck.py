#!/usr/bin/env python3
"""Static checks for the mod's Enforce Script (there is no DayZ script compiler on Linux).

Catches the mistakes most likely without the game at hand:
  - unbalanced braces/brackets/parentheses
  - a script module using a class from a later module (3_Game < 4_World < 5_Mission)
  - calls to RDZ_* classes/methods that are not defined anywhere
  - the same local variable declared twice in one function (an Enforce compile error)
With --vanilla <DayZ-Script-Diff/scripts>, vanilla classes are placed in their modules too.
"""
import argparse
import os
import re
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
MODULES = ["3_Game", "4_World", "5_Mission"]
VANILLA_DIRS = {"1_core": 0, "2_gamelib": 0, "3_game": 1, "4_world": 2, "5_mission": 3}
MOD_LEVEL = {"3_Game": 1, "4_World": 2, "5_Mission": 3}

TYPES = r"(?:void|int|float|bool|string|vector|auto|typename|[A-Z]\w*(?:<[^<>;()]*(?:<[^<>;()]*>)?[^<>;()]*>)?)"


def strip(src):
    """Remove comments and string contents, keep line structure."""
    out, i, n = [], 0, len(src)
    while i < n:
        c = src[i]
        if src.startswith("//", i):
            j = src.find("\n", i)
            i = n if j < 0 else j
        elif src.startswith("/*", i):
            j = src.find("*/", i + 2)
            seg = src[i:(n if j < 0 else j + 2)]
            out.append("\n" * seg.count("\n"))
            i = n if j < 0 else j + 2
        elif c == '"':
            j = i + 1
            while j < n and src[j] != '"':
                j += 2 if src[j] == "\\" else 1
            out.append('""')
            i = j + 1
        else:
            out.append(c)
            i += 1
    return "".join(out)


def mod_files():
    files = []
    for base in (os.path.join(ROOT, "mod", "RivalsDayZ", "scripts"), os.path.join(ROOT, "build", "gen", "RivalsDayZ", "scripts")):
        for mod in MODULES:
            d = os.path.join(base, mod)
            for dp, _, fns in os.walk(d):
                for fn in fns:
                    if fn.endswith(".c"):
                        files.append((mod, os.path.join(dp, fn)))
    return files


def class_defs(src):
    return re.findall(r"^\s*(?:modded\s+)?class\s+(\w+)", src, re.M)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--vanilla")
    args = ap.parse_args()
    errors = []

    vanilla_level = {}
    if args.vanilla:
        for d, lvl in VANILLA_DIRS.items():
            for dp, _, fns in os.walk(os.path.join(args.vanilla, d)):
                for fn in fns:
                    if fn.endswith(".c"):
                        with open(os.path.join(dp, fn), encoding="utf-8", errors="replace") as f:
                            for c in class_defs(strip(f.read())):
                                vanilla_level.setdefault(c, lvl)

    files = mod_files()
    sources = {}
    our_level = {}
    methods = {}  # class -> set(method)
    all_methods = set()
    for mod, path in files:
        with open(path, encoding="utf-8") as f:
            s = strip(f.read())
        sources[path] = (mod, s)
        for m in re.finditer(r"^\s*(modded\s+)?class\s+(\w+)[^{]*\{", s, re.M):
            name = m.group(2)
            if not m.group(1):
                our_level.setdefault(name, MOD_LEVEL[mod])
            # methods of this class body
            depth, i = 1, m.end()
            while i < len(s) and depth:
                depth += {"{": 1, "}": -1}.get(s[i], 0)
                i += 1
            body = s[m.end():i]
            for mm in re.finditer(r"^\s*(?:(?:override|static|protected|private|proto|native|external|owned)\s+)*" + TYPES + r"\s+(\w+)\s*\(", body, re.M):
                methods.setdefault(name, set()).add(mm.group(1))
                all_methods.add(mm.group(1))
            for mm in re.finditer(r"^\s*(?:static\s+|protected\s+|private\s+|const\s+|ref\s+)*" + TYPES + r"\s+(\w+)\s*(?:=|;|\[)", body, re.M):
                methods.setdefault(name, set()).add(mm.group(1))

    for path, (mod, s) in sources.items():
        rel = os.path.relpath(path, ROOT)
        # 1. balance
        for o, c in ("{}", "()", "[]"):
            if s.count(o) != s.count(c):
                errors.append(f"{rel}: unbalanced {o}{c} ({s.count(o)} vs {s.count(c)})")
        lvl = MOD_LEVEL[mod]
        # 2. layering
        type_uses = set(re.findall(r"\bnew\s+([A-Z]\w+)", s))
        type_uses |= set(re.findall(r"\b([A-Z]\w+)\.\w+\s*\(", s))
        type_uses |= set(re.findall(r"(?:extends|:)\s*([A-Z]\w+)\s*\{", s))
        type_uses |= set(re.findall(r"modded\s+class\s+([A-Z]\w+)", s))
        type_uses |= set(re.findall(r"(?:^|[;{(,]|ref\s|const\s|static\s)\s*([A-Z]\w+)(?:<[^;]*?>)?\s+[a-zA-Z_]\w*\s*(?:=|;|,|\)|\[)", s, re.M))
        for ident in type_uses:
            need = our_level.get(ident)
            if need is None:
                need = vanilla_level.get(ident)
            if need is not None and need > lvl:
                errors.append(f"{rel}: {mod} uses {ident}, which is defined in a later module")
        # 3. RDZ calls
        for cls, meth in re.findall(r"\b(RDZ_\w+)\.(\w+)\s*\(", s):
            if cls in methods and meth not in methods[cls] and meth not in ("Cast", "CastTo"):
                errors.append(f"{rel}: {cls}.{meth}() is not defined")
        for meth in set(re.findall(r"\.\s*(RDZ_\w+)\s*\(", s)):
            if meth not in all_methods:
                errors.append(f"{rel}: method {meth}() is not defined in any mod class")
        for ident in set(re.findall(r"\b(RDZ_\w+)\b", s)):
            if ident not in our_level and ident not in all_methods and not any(ident in v for v in methods.values()):
                errors.append(f"{rel}: {ident} is not defined")
        # 4. duplicate locals per function
        for fm in re.finditer(r"^\s*(?:(?:override|static|protected|private)\s+)*" + TYPES + r"\s+\w+\s*\([^;{]*\)\s*\{", s, re.M):
            depth, i = 1, fm.end()
            while i < len(s) and depth:
                depth += {"{": 1, "}": -1}.get(s[i], 0)
                i += 1
            body = s[fm.end():i]
            sig = fm.group(0)
            params = re.findall(TYPES + r"\s+(\w+)\s*(?:,|\)|=)", sig[sig.find("("):])
            seen = {p: "param" for p in params}
            for dm in re.finditer(r"(?:^|[;{}(])\s*(?:const\s+|ref\s+|out\s+)*" + TYPES + r"\s+(\w+)\s*(?:=|;|\[|,|:)", body, re.M):
                name = dm.group(1)
                if name in ("return", "new", "else", "case"):
                    continue
                if name in seen:
                    line = s[:fm.start()].count("\n") + body[:dm.start()].count("\n") + 1
                    errors.append(f"{rel}:{line}: local '{name}' declared twice in one function")
                seen[name] = True

    for e in sorted(set(errors)):
        print("x " + e)
    print(f"escheck: {len(files)} files, {len(set(errors))} problems")
    sys.exit(1 if errors else 0)


if __name__ == "__main__":
    main()
