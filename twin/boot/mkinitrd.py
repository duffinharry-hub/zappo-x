"""Build the twin's start-up ramdisk: Knulli's own ramdisk (unchanged) with a
second archive after it holding the twin's /init and its helper files.  The
kernel unpacks both in order, so the twin's /init replaces Knulli's.

usage: mkinitrd.py KNULLI_RAMDISK.gz INIT OUT.cpio.gz ROOT_DIR [ROOT_DIR...]
(every ROOT_DIR is laid over Knulli's system: twin/root, the graphics pack...)"""
import gzip
import os
import stat
import sys


def newc(entries):
    """entries: (name, mode, data_bytes) -> newc cpio bytes"""
    out = bytearray()
    ino = 900000
    for name, mode, data in entries + [("TRAILER!!!", 0, b"")]:
        ino += 1
        nb = name.encode() + b"\0"
        hdr = "070701" + "".join("%08X" % v for v in (
            ino, mode, 0, 0, 1, 0, len(data), 0, 0, 0, 0, len(nb), 0))
        out += hdr.encode() + nb
        out += b"\0" * ((4 - len(out) % 4) % 4)
        out += data
        out += b"\0" * ((4 - len(out) % 4) % 4)
    return bytes(out)


def tree(root, prefix):
    ents = [(prefix, stat.S_IFDIR | 0o755, b"")]
    for d, dirs, files in os.walk(root):
        dirs.sort()
        rel = os.path.relpath(d, root)
        for n in dirs:
            p = os.path.join(d, n)
            ents.append((os.path.normpath(os.path.join(prefix, rel, n)), stat.S_IFDIR | (os.stat(p).st_mode & 0o7777), b""))
        for n in sorted(files):
            p = os.path.join(d, n)
            st = os.lstat(p)
            name = os.path.normpath(os.path.join(prefix, rel, n))
            if stat.S_ISLNK(st.st_mode):
                ents.append((name, stat.S_IFLNK | 0o777, os.readlink(p).encode()))
            else:
                ents.append((name, stat.S_IFREG | (st.st_mode & 0o7777), open(p, "rb").read()))
    return ents


def main(knulli, init, out, *roots):
    ents = [("init", stat.S_IFREG | 0o755, open(init, "rb").read())]
    ents += [("twin", stat.S_IFDIR | 0o755, b"")]
    seen = {}
    for r in roots:
        for e in tree(r, "twin/root"):
            if e[0] in seen:
                ents[seen[e[0]]] = e        # a later root wins
            else:
                seen[e[0]] = len(ents)
                ents.append(e)
    with open(out, "wb") as f:
        f.write(open(knulli, "rb").read())          # Knulli's ramdisk, as shipped
        f.write(gzip.compress(newc(ents), 9))       # the twin's layer
    print("%s: %d twin files" % (out, len(ents)))


if __name__ == "__main__":
    main(*sys.argv[1:])
