"""Gives the twin the RG40XX H's model name.

QEMU's "virt" machine hands the kernel a device tree whose model is
"linux,dummy-virt"; Knulli reads the model (batocera-info, and from that the
Bluetooth name and the system information screen).  This takes the tree QEMU
made (-machine dumpdtb=...) and puts the handheld's model in its place.

  dtb.py IN.dtb OUT.dtb ["Anbernic RG40XX H"]

The handheld's model string comes from its device tree (sun50i-h700-anbernic-
rg40xx-h); check it on the real one (arrival checklist).
"""
import struct
import sys

FDT_BEGIN_NODE, FDT_END_NODE, FDT_PROP, FDT_NOP, FDT_END = 1, 2, 3, 4, 9


def set_root_prop(dtb, name, value):
    """returns a copy of dtb with the root node's property `name` set to the
    string `value` (rebuilding the structure block, so any length works)"""
    magic, total, off_struct, off_strings, off_rsv, ver, last_comp, boot_cpu, size_strings, size_struct = \
        struct.unpack(">10I", dtb[:40])
    assert magic == 0xD00DFEED
    strings = dtb[off_strings:off_strings + size_strings]
    s = dtb[off_struct:off_struct + size_struct]
    val = value.encode() + b"\0"
    out, i, depth, done = bytearray(), 0, 0, False

    def name_at(off):
        return strings[off:strings.index(b"\0", off)].decode()

    while i < len(s):
        tok, = struct.unpack(">I", s[i:i + 4])
        if tok == FDT_BEGIN_NODE:
            end = s.index(b"\0", i + 4)
            j = (end + 1 + 3) & ~3
            out += s[i:j]
            i = j
            depth += 1
            continue
        if tok == FDT_PROP:
            ln, nameoff = struct.unpack(">II", s[i + 4:i + 12])
            j = (i + 12 + ln + 3) & ~3
            if depth == 1 and name_at(nameoff) == name and not done:
                out += struct.pack(">III", FDT_PROP, len(val), nameoff) + val
                out += b"\0" * (-len(out) % 4)
                done = True
            else:
                out += s[i:j]
            i = j
            continue
        if tok == FDT_END_NODE:
            depth -= 1
        out += s[i:i + 4]
        i += 4
        if tok == FDT_END:
            break
    if not done:
        raise SystemExit("no %s property on the root node" % name)
    # layout: header, reserve map, structure, strings
    rsv = dtb[off_rsv:off_struct] if off_rsv < off_struct else dtb[off_rsv:off_rsv + 16]
    o_rsv = 40
    o_struct = o_rsv + len(rsv)
    o_strings = o_struct + len(out)
    total = max(o_strings + len(strings), 65536)    # room for QEMU to add /chosen
    hdr = struct.pack(">10I", magic, total, o_struct, o_strings, o_rsv, ver, last_comp, boot_cpu,
                      len(strings), len(out))
    body = hdr + rsv + bytes(out) + strings
    return body + b"\0" * (total - len(body))


def main(a):
    model = a[2] if len(a) > 2 else "Anbernic RG40XX H"
    dtb = open(a[0], "rb").read()
    open(a[1], "wb").write(set_root_prop(dtb, "model", model))


if __name__ == "__main__":
    main(sys.argv[1:])
