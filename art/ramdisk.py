"""Puts Zappo's charging pictures into GarlicOS's ramdisk.img.

The RG35XX shows its "charging while switched off" screen from a small Android
program in the boot partition's ramdisk; the pictures are PNGs inside it
(res/images/charger/battery_0..5, battery_charge, battery_fail), drawn centred
on a black screen. This rewrites the ramdisk (a plain cpio archive in a u-boot
image header) with new PNGs; everything else in it stays byte for byte.

usage: ramdisk.py ORIGINAL.img PNGDIR OUT.img
       PNGDIR holds battery_0.png .. battery_5.png, battery_charge.png,
       battery_fail.png (any size; 640x480 fills the screen)."""
import os
import struct
import sys
import zlib

REPLACE = {"battery_%d" % i for i in range(6)} | {"battery_charge", "battery_fail"}


def read_uimage(path):
    d = open(path, "rb").read()
    magic, hcrc, tm, size, load, ep, dcrc, os_, arch, typ, comp = struct.unpack(">IIIIIIIBBBB", d[:32])
    if magic != 0x27051956:
        raise SystemExit("not a u-boot image")
    name = d[32:64]
    return dict(time=tm, load=load, ep=ep, os=os_, arch=arch, type=typ, comp=comp, name=name), d[64:64 + size]


def write_uimage(path, hdr, data):
    dcrc = zlib.crc32(data) & 0xFFFFFFFF
    h = bytearray(struct.pack(">IIIIIIIBBBB", 0x27051956, 0, hdr["time"], len(data), hdr["load"], hdr["ep"], dcrc,
                              hdr["os"], hdr["arch"], hdr["type"], hdr["comp"]) + hdr["name"])
    hcrc = zlib.crc32(bytes(h)) & 0xFFFFFFFF
    h[4:8] = struct.pack(">I", hcrc)
    with open(path, "wb") as f:
        f.write(bytes(h))
        f.write(data)


def parse_cpio(d):
    """[(fields(13 ints), name, data)] in order, TRAILER!!! included last"""
    out = []
    p = 0
    while p + 110 <= len(d):
        if d[p:p + 6] != b"070701":
            raise SystemExit("not a newc cpio archive at %d" % p)
        f = [int(d[p + 6 + i * 8:p + 14 + i * 8], 16) for i in range(13)]
        namesz, fsz = f[11], f[6]
        name = d[p + 110:p + 110 + namesz - 1].decode()
        hs = (110 + namesz + 3) & ~3
        data = d[p + hs:p + hs + fsz]
        out.append((f, name, data))
        p += (hs + fsz + 3) & ~3
        if name == "TRAILER!!!":
            break
    return out


def build_cpio(entries):
    out = bytearray()
    for f, name, data in entries:
        f = list(f)
        f[6] = len(data)
        f[11] = len(name) + 1
        out += b"070701" + b"".join(b"%08X" % v for v in f) + name.encode() + b"\0"
        while len(out) % 4:
            out += b"\0"
        out += data
        while len(out) % 4:
            out += b"\0"
    return bytes(out)


def main(orig, pngdir, dest):
    hdr, cpio = read_uimage(orig)
    entries = parse_cpio(cpio)
    if build_cpio(entries) != cpio.rstrip(b"\0") + b"\0" * (len(build_cpio(entries)) - len(cpio.rstrip(b"\0"))):
        raise SystemExit("the archive did not rebuild byte for byte; stopping rather than guess")
    done = set()
    new = []
    for f, name, data in entries:
        stem = os.path.splitext(os.path.basename(name))[0]
        if name.startswith("res/images/charger/") and stem in REPLACE:
            p = os.path.join(pngdir, stem + ".png")
            if os.path.exists(p):
                data = open(p, "rb").read()
                done.add(stem)
        new.append((f, name, data))
    missing = REPLACE - done
    if missing:
        raise SystemExit("no picture for: " + ", ".join(sorted(missing)))
    data = build_cpio(new)
    data += b"\0" * (-len(data) % 512)  # the original is padded to 512-byte blocks
    write_uimage(dest, hdr, data)
    # check it reads back
    hdr2, cpio2 = read_uimage(dest)
    assert hdr2["load"] == hdr["load"] and parse_cpio(cpio2)[-1][1] == "TRAILER!!!"
    print("ramdisk: %d -> %d bytes, %d pictures replaced" % (len(cpio) + 64, os.path.getsize(dest), len(done)))


if __name__ == "__main__":
    if len(sys.argv) != 4:
        raise SystemExit(__doc__)
    main(*sys.argv[1:])
