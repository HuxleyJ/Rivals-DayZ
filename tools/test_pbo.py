#!/usr/bin/env python3
"""Checks tools/pbo.py against a known-good signed PBO (HEMTT's signing fixture).

Usage: tools/test_pbo.py <dir with source.pbo, test.biprivatekey, test.bikey, source.pbo.test.bisign>
The fixture is not part of this repository; it lives in HEMTT's libs/signing/tests.
"""
import os
import shutil
import sys
import tempfile

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import pbo  # noqa: E402

fx = sys.argv[1]
src = os.path.join(fx, "source.pbo")
p = pbo.read(src)
ok = True


def check(cond, what):
    global ok
    print(("PASS " if cond else "FAIL ") + what)
    ok &= bool(cond)


check(pbo.gen_checksum(p) == p.checksum, "checksum of the canonical PBO equals its trailer")
with tempfile.TemporaryDirectory() as tmp:
    copy = os.path.join(tmp, "source.pbo")
    shutil.copy(src, copy)
    data, authority = pbo.signature_bytes(copy, os.path.join(fx, "test.biprivatekey"), 3)
    with open(os.path.join(fx, "source.pbo.test.bisign"), "rb") as f:
        known = f.read()
    check(authority == "test", "authority read from the private key")
    check(data == known, f"v3 signature byte-identical to the known-good .bisign ({len(data)} bytes)")
    with open(os.path.join(fx, "test.bikey"), "rb") as f:
        bikey = f.read()
    a, length, k = pbo.read_private(os.path.join(fx, "test.biprivatekey"))
    check(pbo.public_bytes(a, length, k["e"], k["n"]) == bikey, "public key bytes match the known .bikey")
    check(pbo.verify(src, os.path.join(fx, "test.bikey"), os.path.join(fx, "source.pbo.test.bisign")), "verify() accepts the known signature")

    # Our own pack -> read -> sign -> verify round trip with a fresh key.
    mod = os.path.join(tmp, "mod")
    os.makedirs(os.path.join(mod, "scripts"))
    open(os.path.join(mod, "config.cpp"), "w").write("class CfgPatches {};\n")
    open(os.path.join(mod, "scripts", "a.c"), "w").write("class A {}\n")
    out = os.path.join(tmp, "Test.pbo")
    pbo.pack(mod, out, "Test")
    q = pbo.read(out)
    check(q.prop("prefix") == "Test" and [e.name for e in q.entries] == ["config.cpp", "scripts\\a.c"], "pack writes prefix and sorted entries")
    check(pbo.gen_checksum(q) == q.checksum, "our PBO trailer equals the canonical checksum")
    priv, pub = pbo.new_key("RivalsDayZTest", tmp)
    for v in (2, 3):
        sig = pbo.sign(out, priv, v)
        check(pbo.verify(out, pub, sig), f"fresh key v{v} signature verifies")
    with open(out, "r+b") as f:
        f.seek(60)
        f.write(b"X")
    check(not pbo.verify(out, pub, sig), "tampered PBO fails verification")

print("ALL PASSED" if ok else "FAILURES")
sys.exit(0 if ok else 1)
