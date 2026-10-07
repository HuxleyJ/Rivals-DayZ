#!/usr/bin/env python3
"""Build the Rivals x DayZ release.

  1. preflight the design sheets (stops on anything blocking) and statically check the scripts
  2. generate code from the sheets
  3. stage, pack and sign RivalsDayZ.pbo
  4. publish the Windows launcher (RivalsDayZ.exe) and bundle vgmstream
  5. assemble build/release/@RivalsDayZ and zip it

The signing key is created on first build in build/keys (never committed). Everything a
player needs is in the zip; nothing from DayZ or Marvel Rivals is in it.
"""
import argparse
import hashlib
import json
import os
import shutil
import subprocess
import sys
import urllib.request
import zipfile

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import pbo  # noqa: E402
from preflight import ROOT, load_sheets, preflight  # noqa: E402

BUILD = os.path.join(ROOT, "build")
STAGE = os.path.join(BUILD, "stage", "RivalsDayZ")
RELEASE = os.path.join(BUILD, "release")
MODDIR = os.path.join(RELEASE, "@RivalsDayZ")
KEYS = os.path.join(BUILD, "keys")
AUTHORITY = "RivalsDayZ"
VGMSTREAM_URL = "https://github.com/vgmstream/vgmstream/releases/latest/download/vgmstream-win64.zip"


def step(msg):
    print(f"\n== {msg}")


def run(cmd, cwd=None):
    print("$ " + " ".join(cmd))
    subprocess.run(cmd, cwd=cwd, check=True)


def version():
    with open(os.path.join(ROOT, "launcher", "RivalsDayZ.Launcher", "RivalsDayZ.Launcher.csproj"), encoding="utf-8") as f:
        src = f.read()
    return src.split("<Version>")[1].split("</Version>")[0]


def sign_version():
    for r in load_sheets()["multiplayer"]["rows"]:
        if r["id"] == "SIGN_VERSION":
            return int(r["value"])
    return 3


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--vanilla", help="DayZ-Script-Diff/scripts for the script checks")
    ap.add_argument("--skip-launcher", action="store_true")
    args = ap.parse_args()

    step("Preflight")
    blocking, open_checks, planned = preflight(args.vanilla)
    for b in blocking:
        print("  x " + b)
    if blocking:
        sys.exit("Preflight has blocking items; fix the sheets first.")
    print(f"  clean: {len(open_checks)} open checks need the game, {len(planned)} planned rows")

    step("Generate code from sheets")
    run([sys.executable, os.path.join(ROOT, "tools", "gen.py")])

    step("Static script checks")
    cmd = [sys.executable, os.path.join(ROOT, "tools", "escheck.py")]
    if args.vanilla:
        cmd += ["--vanilla", args.vanilla]
    run(cmd)

    step("Stage mod files")
    shutil.rmtree(os.path.join(BUILD, "stage"), ignore_errors=True)
    shutil.copytree(os.path.join(ROOT, "mod", "RivalsDayZ"), STAGE, ignore=shutil.ignore_patterns("config.cpp.in"))
    shutil.copytree(os.path.join(BUILD, "gen", "RivalsDayZ"), STAGE, dirs_exist_ok=True)

    step("Pack and sign RivalsDayZ.pbo")
    shutil.rmtree(RELEASE, ignore_errors=True)
    addons = os.path.join(MODDIR, "addons")
    out_pbo = os.path.join(addons, "RivalsDayZ.pbo")
    print(f"  {pbo.pack(STAGE, out_pbo, 'RivalsDayZ')} files packed")
    priv = os.path.join(KEYS, AUTHORITY + ".biprivatekey")
    pub = os.path.join(KEYS, AUTHORITY + ".bikey")
    if not os.path.exists(priv):
        pbo.new_key(AUTHORITY, KEYS)
        print("  created signing key in build/keys (keep it out of git)")
    sig = pbo.sign(out_pbo, priv, sign_version())
    if not pbo.verify(out_pbo, pub, sig):
        sys.exit("Signature does not verify.")
    os.makedirs(os.path.join(MODDIR, "keys"))
    shutil.copy(pub, os.path.join(MODDIR, "keys"))
    print(f"  signed (v{sign_version()}) and verified: {os.path.basename(sig)}")

    step("Mission, server files and mod.cpp")
    shutil.copytree(os.path.join(ROOT, "mission"), os.path.join(MODDIR, "Missions"))
    shutil.copytree(os.path.join(ROOT, "server"), os.path.join(MODDIR, "server"))
    with open(os.path.join(MODDIR, "mod.cpp"), "w", encoding="utf-8", newline="\n") as f:
        f.write('name = "Rivals x DayZ";\n'
                'author = "Rivals x DayZ";\n'
                f'version = "{version()}";\n'
                'overview = "Spider-Man and Rogue from your Marvel Rivals install, in DayZ. Web-swing, crawl walls, drain and steal powers.";\n')
    shutil.copy(os.path.join(ROOT, "THIRD_PARTY.md"), MODDIR)
    shutil.copy(os.path.join(ROOT, "README.md"), MODDIR)

    if not args.skip_launcher:
        step("Launcher (RivalsDayZ.exe)")
        proj = os.path.join(ROOT, "launcher", "RivalsDayZ.Launcher")
        run(["dotnet", "publish", "-c", "Release", "-o", os.path.join(BUILD, "launcher")], cwd=proj)
        shutil.copy(os.path.join(BUILD, "launcher", "RivalsDayZ.exe"), MODDIR)

        step("vgmstream (Wwise audio decoder)")
        cache = os.path.join(BUILD, "cache", "vgmstream-win64.zip")
        if not os.path.exists(cache):
            os.makedirs(os.path.dirname(cache), exist_ok=True)
            urllib.request.urlretrieve(VGMSTREAM_URL, cache)
        with open(cache, "rb") as f:
            print("  vgmstream-win64.zip sha256 " + hashlib.sha256(f.read()).hexdigest())
        with zipfile.ZipFile(cache) as z:
            z.extractall(os.path.join(MODDIR, "tools", "vgmstream"))

    step("Zip")
    zip_path = os.path.join(RELEASE, f"RivalsDayZ-{version()}.zip")
    with zipfile.ZipFile(zip_path, "w", zipfile.ZIP_DEFLATED) as z:
        for dp, _, fns in os.walk(MODDIR):
            for fn in sorted(fns):
                full = os.path.join(dp, fn)
                z.write(full, os.path.relpath(full, RELEASE))
    with open(zip_path, "rb") as f:
        digest = hashlib.sha256(f.read()).hexdigest()
    size = os.path.getsize(zip_path)
    manifest = {"file": os.path.basename(zip_path), "bytes": size, "sha256": digest, "version": version(),
                "open_checks": len(open_checks), "planned": len(planned)}
    with open(os.path.join(RELEASE, "release.json"), "w", encoding="utf-8") as f:
        json.dump(manifest, f, indent=2)
    print(f"  {zip_path}\n  {size} bytes, sha256 {digest}")


if __name__ == "__main__":
    main()
