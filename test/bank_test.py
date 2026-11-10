#!/usr/bin/env python3
"""tools/mkrom.py's samples put into a firmware package, against fm1-base's tools/fm1pkg_make.py: a package with
samples put in must be the very package fm1pkg_make.py makes of the firmware with those samples at its slot's top, and
--none must give the package back as it was. With a made-up firmware (an image holding app/s700/s7bank.h's
description of itself) and made-up SDK files, so it needs neither a device build nor JieLi's SDK.

  .venv/bin/python test/bank_test.py      (test/run.sh does it)
"""
import random
import struct
import subprocess
import sys
import tempfile
import zlib
from pathlib import Path

import numpy as np

ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / "tools"))
import mkrom  # noqa: E402

P = mkrom.fm1pkg()
XIP = 0x02000120
failed = 0
checks = 0


def check(ok, what):
    global failed, checks
    checks += 1
    if not ok:
        failed += 1
        print(f"FAIL: {what}")


def refused(fn, what):
    try:
        fn()
    except SystemExit as e:
        check(True, what)
        return str(e)
    check(False, f"{what}: not refused")
    return ""


rnd = random.Random(87)
tmp = Path(tempfile.mkdtemp(prefix="sm87-bank-"))
sdk = tmp / "sdk" / "cpu" / "wl82" / "tools"
(sdk / "cfg").mkdir(parents=True)
(sdk / "uboot.boot").write_bytes(bytes(rnd.randrange(256) for _ in range(14536)))
(sdk / "cfg_tool.bin").write_bytes(bytes(rnd.randrange(256) for _ in range(383)))   # (the app area's end mid-block)
(sdk / "cfg" / "eq_cfg_hw.bin").write_bytes(bytes(rnd.randrange(256) for _ in range(300)))
P.SDK = tmp / "sdk"
OTA = bytes(rnd.randrange(256) for _ in range(11782))


def firmware(size=200916, at=0x2657C, version=mkrom.BANK_VERSION, data=0x1A8):
    """an image of size bytes with the description of itself at `at`, .data's load image last"""
    img = bytearray(rnd.randrange(255) for _ in range(size))     # (no 0xFF: its end can't be taken for padding)
    img[at:at + 36] = mkrom.DESC_MAGIC + struct.pack("<6I", version, XIP + at, XIP + size - data, 0x01C08000,
                                                     0x01C08000 + data, XIP + P.APP_SLOT)
    return bytes(img)


def package(app, key=P.KEY, product="FM-1_8700"):
    return P.ufw(P.flash_image(app, key), OTA, product)


def with_bank(app, b):
    return app + b"\xFF" * (P.APP_SLOT - len(app) - len(b)) + b


def ramp(n, step):
    return np.arange(n, dtype=np.int64) * step % 4096


p1 = mkrom.default_params()
p1[0], p1[1] = 24, 99                 # P1, keys 24-99
p2 = mkrom.default_params()
p2[4 * 5], p2[4 * 5 + 1], p2[mkrom.P_RECBW] = 36, 60, 60
ITEMS = [(0, "ramp", ramp(32752, 7), p1, 0),
         (5, "a name longer than the bank keeps", ramp(1001, 3), p2, 40)]

app = firmware()
orig = package(app)
pk = mkrom.Package(orig, "test")
check(pk.identity == "FM-1_8700", f"the identity: {pk.identity}")
check(pk.image_end == len(app) and len(pk.app) == P.APP_SLOT, f"the image ends at {pk.image_end}")
check(pk.room() == P.APP_SLOT - len(app), f"the room: {pk.room()}")
check(pk.samples() is None, "no samples in a package as built")

# the bank's bytes: entries, the words packed, the footer
b = mkrom.bank(ITEMS)
check(len(b) % 4 == 0, "the bank: whole words")
check(len(b) == 2 * 188 + (mkrom.packed_size(32752) + 3) // 4 * 4 + (mkrom.packed_size(1001) + 3) // 4 * 4 + 32,
      f"the bank: {len(b)} bytes")
foot = b[-32:]
check(struct.unpack_from("<IHHII", foot) == (0x4B423753, 1, 2, len(b) - 32, zlib.crc32(b[:-32])), "the footer")
check(struct.unpack_from("<I", foot, 28)[0] == zlib.crc32(foot[:28]), "the footer's own CRC")
e = struct.unpack_from("<BBHI24s", b, 188)
check(e[:4] == (5, 40, 1001, 2 * 188 + (mkrom.packed_size(32752) + 3) // 4 * 4), f"the second entry: {e[:4]}")
check(e[4] == b"a name longer than the b" [:23].ljust(24, b"\0"), f"its name cut to 23: {e[4]}")
check(b[188 + 32:188 + 32 + 155] == bytes(p2), "its parameters")
check(b[2 * 188:2 * 188 + mkrom.packed_size(32752)] == mkrom.pack(ITEMS[0][2]), "the first's words, packed")

# into the package: fm1pkg_make's package of the firmware with the bank at the slot's top
pk.put(b)
baked = pk.close()
check(baked == package(with_bank(app, b)), "samples in: fm1pkg_make.py's package of the firmware with them")
got = mkrom.Package(baked, "baked").samples()
check(got is not None and [(s, n, w, p, pr) for s, n, w, p, pr in got] ==
      [(0, "ramp", 32752, p1, 0), (5, "a name longer than the ", 1001, p2, 40)], f"read back: {got and got[1][:3]}")

# again, fewer: the old ones gone; then none: the package as built
pk2 = mkrom.Package(baked, "baked")
b2 = mkrom.bank(ITEMS[1:])
pk2.put(b2)
check(pk2.close() == package(with_bank(app, b2)), "samples in again: the old ones gone")
pk3 = mkrom.Package(baked, "baked")
pk3.put(b"")
check(pk3.close() == orig, "--none: the package as built, byte for byte")

# all the room, and a byte more
full = mkrom.Package(orig, "test")
each = 188 + (mkrom.packed_size(32752) + 3) // 4 * 4      # a full-length sample, its entry with it
items = [(i, f"s{i}", ramp(32752, i + 1), p1, 0) for i in range(full.room() // each + 1)]
big = mkrom.bank(items)
check(len(big) > full.room(), f"{len(big)} bytes for {full.room()}")
refused(lambda: full.put(big), "more than the room refused")
fit = mkrom.bank(items[:-1])
full.put(fit)
check(full.close() == package(with_bank(app, fit)), f"{len(fit)} bytes of {full.room()}: in")

# another chip key, another identity
other = package(app, key=0x1234, product="FM-1_87010203")
pk4 = mkrom.Package(other, "other")
check(pk4.key == 0x1234 and pk4.identity == "FM-1_87010203", f"chip key {pk4.key:#06x}, {pk4.identity}")
pk4.put(b)
check(pk4.close() == package(with_bank(app, b), key=0x1234, product="FM-1_87010203"), "another chip key: the same")

# refused: a firmware without the description, another format, a damaged package, something above the image
refused(lambda: mkrom.Package(package(bytes(rnd.randrange(255) for _ in range(150000))), "x"),
        "a firmware without the bank's description refused")
why = refused(lambda: mkrom.Package(package(firmware(version=2)), "x"), "another bank format refused")
check("format 2" in why, f"... saying so: {why}")
bad = bytearray(orig)
bad[5000] ^= 1
refused(lambda: mkrom.Package(bytes(bad), "x"), "a damaged package refused")
stray = bytearray(app + b"\xFF" * 1000)
stray[len(app) + 500] = 0
pk5 = mkrom.Package(package(bytes(stray)), "x")
refused(lambda: pk5.put(b), "something above the firmware that isn't samples: refused")

# the command, as the README has it: in place, -o, the package alone, --none
wav = tmp / "sine.wav"
subprocess.run([sys.executable, "-I", str(ROOT / "test" / "make_test_wav.py"), str(wav)], check=True)
f = tmp / "t.fwsc"
f.write_bytes(orig)
run = lambda *a: subprocess.run([sys.executable, "-I", str(ROOT / "tools" / "mkrom.py"), *map(str, a)],  # noqa: E731
                                capture_output=True, text=True)
r = run(f, f"{wav}@3", "-o", tmp / "o.fwsc")
check(r.returncode == 0 and f.read_bytes() == orig and (tmp / "o.fwsc").exists(), f"-o: another file ({r.stderr})")
r = run(f, f"{wav}@3")
check(r.returncode == 0 and f.read_bytes() == (tmp / "o.fwsc").read_bytes(), "without -o: in place")
r = run(f)
check(r.returncode == 0 and "slot  3  sine" in r.stdout and "1 samples" in r.stdout, f"the package alone: {r.stdout}")
r = run(f, "--none")
check(r.returncode == 0 and f.read_bytes() == orig, "--none")
r = run(tmp / "sine.wav", "x.wav")
check(r.returncode != 0 and "package" in r.stderr, "a WAV first: refused")

# fm1-base's web installer takes it (web/fm1pkg.js checkUfw), when there's node to ask
(tmp / "baked.fwsc").write_bytes(baked)
js = ("import { readFileSync } from 'fs';"
      f"import {{ productOf, logicalImage, checkUfw }} from '{ROOT / 'fm1-base' / 'web' / 'fm1pkg.js'}';"
      f"const raw = new Uint8Array(readFileSync('{tmp / 'baked.fwsc'}'));"
      "checkUfw(logicalImage(raw)); console.log(productOf(raw));")
try:
    r = subprocess.run(["node", "--input-type=module", "-e", js], capture_output=True, text=True)
    check(r.returncode == 0 and r.stdout.strip() == "FM-1_8700", f"the web installer's check: {r.stderr.strip()}")
except FileNotFoundError:
    print("bank_test: no node, the web installer's check not made")

print(f"bank_test: {checks - failed} of {checks} checks passed")
sys.exit(1 if failed else 0)
