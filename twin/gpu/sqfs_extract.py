#!/usr/bin/env python3
"""Pulls files out of Knulli's system image (a squashfs, gzip) without
mounting it: enough of the squashfs 4 format for regular files, symlinks and
directories.  Used to bring back twin/knulli/rootfs for the build sysroot
(twin/gpu/mksysroot.py links into it) when the container lost it.

  sqfs_extract.py IMAGE OFFSET OUT_DIR PATH...      (PATH inside the image)
  sqfs_extract.py IMAGE OFFSET OUT_DIR --sysroot SYSROOT
      (every file the sysroot's links into OUT_DIR point at)
"""
import os
import struct
import sys
import zlib


class Sqfs:
    def __init__(self, path, base):
        self.f = open(path, "rb")
        self.base = base
        sb = self.read(0, 96)
        if sb[:4] != b"hsqs":
            raise SystemExit("no squashfs at %d" % base)
        (self.inodes, _, self.block_size, self.frag_count, self.comp, _, _, _, _, _, self.root_ref, self.bytes_used,
         self.id_start, _, self.inode_start, self.dir_start, self.frag_start, _) = struct.unpack("<IIIIHHHHHHQQQQQQQQ", sb[4:96])
        if self.comp != 1:
            raise SystemExit("compression %d: only gzip is understood here" % self.comp)
        self.meta_cache = {}
        self.frags = None

    def read(self, off, n):
        self.f.seek(self.base + off)
        return self.f.read(n)

    def meta_block(self, pos):
        """one metadata block at pos: (its bytes, where the next one starts)"""
        if pos in self.meta_cache:
            return self.meta_cache[pos]
        h = struct.unpack("<H", self.read(pos, 2))[0]
        size = h & 0x7FFF
        raw = self.read(pos + 2, size)
        data = raw if h & 0x8000 else zlib.decompress(raw)
        self.meta_cache[pos] = (data, pos + 2 + size)
        return self.meta_cache[pos]

    def meta(self, table, block, offset, n):
        """n bytes of a metadata table, from block (relative) + offset"""
        pos = table + block
        out = b""
        while len(out) < n:
            data, nxt = self.meta_block(pos)
            if offset >= len(data):	# (starts in a later block)
                offset -= len(data)
                pos = nxt
                continue
            out += data[offset:]
            offset = 0
            pos = nxt
        return out[:n]

    def inode(self, ref):
        blk, off = ref >> 16, ref & 0xFFFF
        typ = struct.unpack("<H", self.meta(self.inode_start, blk, off, 2))[0]
        # (read as one: an inode can start near the end of a metadata block)
        whole = self.meta(self.inode_start, blk, off, 16 + (64 * 1024 if typ in (2, 9) else 4096 + 64))
        return typ, struct.unpack("<H", whole[2:4])[0], whole[16:]

    def listdir(self, ref):
        typ, _, b = self.inode(ref)
        if typ == 1:
            start, _, size, off = struct.unpack("<IIHH", b[:12])
        elif typ == 8:
            _, size, start, _, _, off = struct.unpack("<IIIIHH", b[:20])
        else:
            raise ValueError("not a directory")
        size -= 3
        data = self.meta(self.dir_start, start, off, size) if size > 0 else b""
        out, p = {}, 0
        while p + 12 <= len(data):
            count, istart, _ = struct.unpack("<III", data[p:p + 12])
            p += 12
            for _ in range(count + 1):
                eoff, _, etype, nsize = struct.unpack("<HhHH", data[p:p + 8])
                name = data[p + 8:p + 9 + nsize].decode("utf-8", "surrogateescape")
                p += 9 + nsize
                out[name] = (istart << 16) | eoff
        return out

    def lookup(self, path):
        ref = self.root_ref
        for part in [x for x in path.split("/") if x]:
            ref = self.listdir(ref).get(part)
            if ref is None:
                return None
        return ref

    def fragment(self, idx):
        if self.frags is None:
            n = (self.frag_count + 511) // 512
            ptrs = struct.unpack("<%dQ" % n, self.read(self.frag_start, 8 * n))
            self.frags = []
            for i, ptr in enumerate(ptrs):
                cnt = min(512, self.frag_count - i * 512)
                data = self.meta(ptr, 0, 0, cnt * 16)
                self.frags += [struct.unpack("<QII", data[k * 16:k * 16 + 16])[:2] for k in range(cnt)]
        start, size = self.frags[idx]
        raw = self.read(start, size & 0xFFFFFF)
        return raw if size & 0x1000000 else zlib.decompress(raw)

    def file_data(self, typ, b):
        if typ == 2:
            start, frag, foff, size = struct.unpack("<IIII", b[:16])
            rest = b[16:]
        else:
            start, size, _, _, frag, foff, _ = struct.unpack("<QQQIIII", b[:40])
            rest = b[40:]
        nblocks = size // self.block_size if frag != 0xFFFFFFFF else (size + self.block_size - 1) // self.block_size
        sizes = struct.unpack("<%dI" % nblocks, rest[:4 * nblocks])
        out, pos = [], start
        for s in sizes:
            n = s & 0xFFFFFF
            if n == 0:
                out.append(b"\0" * self.block_size)
                continue
            raw = self.read(pos, n)
            pos += n
            out.append(raw if s & 0x1000000 else zlib.decompress(raw))
        data = b"".join(out)
        if frag != 0xFFFFFFFF:
            fr = self.fragment(frag)
            data += fr[foff:foff + size % self.block_size]
        return data[:size]

    def extract(self, path, out_dir, seen=None, depth=0):
        """path (and, for a link, what it points at) into out_dir"""
        seen = set() if seen is None else seen
        path = os.path.normpath("/" + path).lstrip("/")
        if path in seen or depth > 20:
            return 0
        seen.add(path)
        ref = self.lookup(path)
        if ref is None:
            return 0
        typ, mode, b = self.inode(ref)
        dst = os.path.join(out_dir, path)
        os.makedirs(os.path.dirname(dst), exist_ok=True)
        if typ in (3, 10):
            n = struct.unpack("<I", b[4:8])[0]
            target = b[8:8 + n].decode("utf-8", "surrogateescape")
            if not os.path.lexists(dst):
                os.symlink(target, dst)
            nxt = target.lstrip("/") if target.startswith("/") else os.path.join(os.path.dirname(path), target)
            return 1 + self.extract(nxt, out_dir, seen, depth + 1)
        if typ in (1, 8):
            os.makedirs(dst, exist_ok=True)
            return sum(self.extract(path + "/" + name, out_dir, seen, depth) for name in self.listdir(ref))
        if typ in (2, 9):
            if not os.path.exists(dst):
                with open(dst + ".part", "wb") as f:
                    f.write(self.file_data(typ, b))
                os.chmod(dst + ".part", mode & 0o777)
                os.replace(dst + ".part", dst)
            return 1
        return 0


def main():
    img, off, out = sys.argv[1], int(sys.argv[2]), os.path.realpath(sys.argv[3])
    fs = Sqfs(img, off)
    paths = sys.argv[4:]
    if paths[:1] == ["--sysroot"]:
        paths = []
        for dirpath, dirs, files in os.walk(sys.argv[5]):
            for n in dirs + files:
                p = os.path.join(dirpath, n)
                if os.path.islink(p):
                    t = os.readlink(p)
                    if t.startswith(out + "/"):
                        paths.append(t[len(out) + 1:])
    n = 0
    for p in sorted(set(paths)):
        n += fs.extract(p, out)
    print("%d files and links from %d paths into %s" % (n, len(set(paths)), out))


if __name__ == "__main__":
    main()
