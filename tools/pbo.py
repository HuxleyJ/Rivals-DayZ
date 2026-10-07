#!/usr/bin/env python3
"""PBO packing and Bohemia (BI) signing for DayZ mods, in plain Python.

  pack(src_dir, out_pbo, prefix)        uncompressed PBO, files sorted, SHA1 trailer
  read(pbo) -> Pbo                      parse headers/properties/data
  new_key(authority, out_dir)           <authority>.biprivatekey + <authority>.bikey (RSA 1024, e=65537)
  sign(pbo, biprivatekey, version)      writes <pbo>.<authority>.bisign
  verify(pbo, bikey, bisign) -> bool

The signature layout and hash rules follow the published format (also implemented by
HEMTT); tools/test_pbo.py checks this module against a known-good signed PBO.
"""
import hashlib
import os
import struct
import sys
import time

V2_EXT = {"fxy", "jpg", "lip", "ogg", "p3d", "paa", "pac", "png", "rtm", "rvmat", "tga", "wrp", "wss"}
V3_EXT = {"bikb", "cfg", "ext", "fsm", "h", "hpp", "inc", "sqf", "sqfc", "sqm", "sqs"}
NOTHING = {2: b"nothing", 3: b"gnihton"}
VERS = 0x56657273


class Entry:
    def __init__(self, name, mime, original, reserved, timestamp, size):
        self.name, self.mime, self.original, self.reserved, self.timestamp, self.size = name, mime, original, reserved, timestamp, size
        self.data = b""


class Pbo:
    def __init__(self):
        self.properties = []  # ordered (key, value)
        self.entries = []
        self.checksum = b""
        self.vers_raw = b""

    def prop(self, key):
        for k, v in self.properties:
            if k == key:
                return v
        return None

    def sorted_entries(self):
        return sorted(self.entries, key=lambda e: e.name.lower())


def _cstr(data, pos):
    end = data.index(b"\0", pos)
    return data[pos:end].decode("latin-1"), end + 1


def read(path):
    with open(path, "rb") as f:
        data = f.read()
    p = Pbo()
    pos = 0
    while True:
        start = pos
        name, pos = _cstr(data, pos)
        mime, original, reserved, timestamp, size = struct.unpack_from("<5I", data, pos)
        pos += 20
        if name == "" and mime == VERS:
            p.vers_raw = data[start:pos]
            while True:
                k, pos = _cstr(data, pos)
                if k == "":
                    break
                v, pos = _cstr(data, pos)
                p.properties.append((k, v))
            continue
        if name == "":
            break
        p.entries.append(Entry(name, mime, original, reserved, timestamp, size))
    for e in p.entries:
        e.data = data[pos:pos + e.size]
        pos += e.size
    if len(data) >= pos + 21 and data[pos] == 0:
        p.checksum = data[pos + 1:pos + 21]
    return p


def _entry_bytes(name, mime=0, original=0, reserved=0, timestamp=0, size=0):
    return name.encode("latin-1") + b"\0" + struct.pack("<5I", mime, original, reserved, timestamp, size)


def pack(src_dir, out_pbo, prefix):
    files = []
    for dp, _, fns in os.walk(src_dir):
        for fn in fns:
            full = os.path.join(dp, fn)
            rel = os.path.relpath(full, src_dir).replace("/", "\\")
            files.append((rel, full))
    files.sort(key=lambda f: f[0].lower())
    stamp = int(time.time()) if not os.environ.get("SOURCE_DATE_EPOCH") else int(os.environ["SOURCE_DATE_EPOCH"])
    body = bytearray()
    body += _entry_bytes("", VERS)
    body += b"prefix\0" + prefix.encode("latin-1") + b"\0" + b"\0"
    blobs = []
    for rel, full in files:
        with open(full, "rb") as f:
            blob = f.read()
        blobs.append(blob)
        body += _entry_bytes(rel, 0, 0, 0, stamp, len(blob))
    body += _entry_bytes("")
    for blob in blobs:
        body += blob
    digest = hashlib.sha1(bytes(body)).digest()
    os.makedirs(os.path.dirname(os.path.abspath(out_pbo)), exist_ok=True)
    with open(out_pbo, "wb") as f:
        f.write(bytes(body) + b"\0" + digest)
    return len(files)


# --- hashes -------------------------------------------------------------------------

def gen_checksum(p):
    """SHA1 over the canonical (sorted) PBO as the engine sees it; equals the trailer for sorted PBOs."""
    h = bytearray(p.vers_raw or _entry_bytes("", VERS))
    if not p.vers_raw:
        pass
    pre = p.prop("prefix")
    if pre is not None:
        h += b"prefix\0" + pre.encode("latin-1") + b"\0"
    for k, v in p.properties:
        if k != "prefix":
            h += k.encode("latin-1") + b"\0" + v.encode("latin-1") + b"\0"
    h += b"\0"
    for e in p.sorted_entries():
        h += _entry_bytes(e.name, e.mime, e.original, e.reserved, e.timestamp, e.size)
    h += _entry_bytes("")
    sha = hashlib.sha1(bytes(h))
    for e in p.sorted_entries():
        sha.update(e.data)
    return sha.digest()


def hash_filenames(p):
    sha = hashlib.sha1()
    for e in p.sorted_entries():
        if (e.original if e.mime == 0x43707273 else e.size) == 0:
            continue
        sha.update(e.name.replace("/", "\\").lower().encode("latin-1"))
    return sha.digest()


def hash_files(p, version):
    allowed = V2_EXT if version == 2 else V3_EXT
    sha = hashlib.sha1()
    nothing = True
    for e in p.sorted_entries():
        ext = e.name.rsplit(".", 1)[-1].lower() if "." in e.name.replace("\\", "/").rsplit("/", 1)[-1] else ""
        if ext not in allowed:
            continue
        nothing = False
        sha.update(e.data)
    if nothing:
        sha.update(NOTHING[version])
    return sha.digest()


def _prefix_bytes(p):
    pre = p.prop("prefix")
    if pre is None:
        return b""
    b = pre.encode("latin-1")
    return b if pre.endswith("\\") else b + b"\\"


def generate_hashes(p, version):
    h1 = gen_checksum(p)
    names = hash_filenames(p)
    h2 = hashlib.sha1(h1 + names + _prefix_bytes(p)).digest()
    h3 = hashlib.sha1(hash_files(p, version) + names + _prefix_bytes(p)).digest()
    return h1, h2, h3


def pad_hash(h, size):
    b = b"\x00\x01" + b"\xff" * (size - 36 - 2) + b"\x00\x30\x21\x30\x09\x06\x05\x2b\x0e\x03\x02\x1a\x05\x00\x04\x14" + h
    assert len(b) == size
    return int.from_bytes(b, "big")


# --- keys ------------------------------------------------------------------------------

def _le(n, size):
    return n.to_bytes(size, "little")


def _cstring(s):
    return s.encode("latin-1") + b"\0"


def read_private(path):
    with open(path, "rb") as f:
        d = f.read()
    authority, pos = _cstr(d, 0)
    temp, _, _, _, length = struct.unpack_from("<5I", d, pos)
    pos += 20
    if temp != length // 16 * 9 + 20:
        raise ValueError("not a BI private key")
    fields = {}
    for name, size in (("e", 4), ("n", length // 8), ("p", length // 16), ("q", length // 16), ("dp", length // 16),
                       ("dq", length // 16), ("qinv", length // 16), ("d", length // 8)):
        fields[name] = int.from_bytes(d[pos:pos + size], "little")
        pos += size
    return authority, length, fields


def read_public(path):
    with open(path, "rb") as f:
        d = f.read()
    authority, pos = _cstr(d, 0)
    pos += 4 + 8 + 4  # length field, blob header, "RSA1"
    length, = struct.unpack_from("<I", d, pos)
    pos += 4
    e = int.from_bytes(d[pos:pos + 4], "little")
    n = int.from_bytes(d[pos + 4:pos + 4 + length // 8], "little")
    return authority, length, e, n


def public_bytes(authority, length, e, n):
    return (_cstring(authority) + struct.pack("<I", length // 8 + 20) + b"\x06\x02\x00\x00\x00\x24\x00\x00" + b"RSA1"
            + struct.pack("<I", length) + _le(e, 4) + _le(n, length // 8))


def new_key(authority, out_dir, length=1024):
    from cryptography.hazmat.primitives.asymmetric import rsa
    key = rsa.generate_private_key(public_exponent=65537, key_size=length)
    nums = key.private_numbers()
    pub = nums.public_numbers
    priv = (_cstring(authority) + struct.pack("<I", length // 16 * 9 + 20) + b"\x07\x02\x00\x00\x00\x24\x00\x00" + b"RSA2"
            + struct.pack("<I", length) + _le(pub.e, 4) + _le(pub.n, length // 8)
            + _le(nums.p, length // 16) + _le(nums.q, length // 16) + _le(nums.dmp1, length // 16)
            + _le(nums.dmq1, length // 16) + _le(nums.iqmp, length // 16) + _le(nums.d, length // 8))
    os.makedirs(out_dir, exist_ok=True)
    priv_path = os.path.join(out_dir, authority + ".biprivatekey")
    pub_path = os.path.join(out_dir, authority + ".bikey")
    with open(priv_path, "wb") as f:
        f.write(priv)
    with open(pub_path, "wb") as f:
        f.write(public_bytes(authority, length, pub.e, pub.n))
    return priv_path, pub_path


def signature_bytes(pbo_path, private_path, version):
    authority, length, k = read_private(private_path)
    p = read(pbo_path)
    size = length // 8
    sigs = [pow(pad_hash(h, size), k["d"], k["n"]) for h in generate_hashes(p, version)]
    return (public_bytes(authority, length, k["e"], k["n"])
            + struct.pack("<I", size) + _le(sigs[0], size)
            + struct.pack("<I", version)
            + struct.pack("<I", size) + _le(sigs[1], size)
            + struct.pack("<I", size) + _le(sigs[2], size)), authority


def sign(pbo_path, private_path, version=3):
    data, authority = signature_bytes(pbo_path, private_path, version)
    out = f"{pbo_path}.{authority}.bisign"
    with open(out, "wb") as f:
        f.write(data)
    return out


def verify(pbo_path, bikey_path, bisign_path):
    _, length, e, n = read_public(bikey_path)
    with open(bisign_path, "rb") as f:
        d = f.read()
    _, pos = _cstr(d, 0)
    pos += 4 + 8 + 4 + 4 + 4 + length // 8
    size = length // 8

    def take():
        nonlocal pos
        ln, = struct.unpack_from("<I", d, pos)
        v = int.from_bytes(d[pos + 4:pos + 4 + ln], "little")
        pos += 4 + ln
        return v

    s1 = take()
    version, = struct.unpack_from("<I", d, pos)
    pos += 4
    s2 = take()
    s3 = take()
    hashes = generate_hashes(read(pbo_path), version)
    return all(pow(s, e, n) == pad_hash(h, size) for s, h in zip((s1, s2, s3), hashes))


if __name__ == "__main__":
    cmd = sys.argv[1:]
    if cmd and cmd[0] == "pack":
        print(pack(cmd[1], cmd[2], cmd[3]), "files packed")
    elif cmd and cmd[0] == "verify":
        print("valid" if verify(cmd[1], cmd[2], cmd[3]) else "INVALID")
    else:
        print(__doc__)
