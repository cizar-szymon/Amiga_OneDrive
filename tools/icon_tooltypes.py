#!/usr/bin/env python3
"""Show or replace the tool types of an Amiga .info file (OS 3.x DiskObject,
optionally followed by an OS 3.5 FORM ICON chunk, which is kept).

  python tools/icon_tooltypes.py WizardOfWor.info                  show them
  python tools/icon_tooltypes.py WizardOfWor.info SCALE=2 "(NOSOUND)"   replace them
"""

import struct
import sys


def image_size(d, off):
    w, h, depth, data, pick, onoff, nxt = struct.unpack(">HHHIBBI", d[off + 4:off + 20])
    return 20 + ((w + 15) // 16) * 2 * h * depth if data else 20


def parse(d):
    """offset of the tool types and of what follows them"""
    render, select = struct.unpack(">II", d[4 + 18:4 + 26])
    deftool, tooltypes = struct.unpack(">II", d[50:58])
    drawer = struct.unpack(">I", d[66:70])[0]
    off = 78
    if drawer:
        off += 56                                  # OS 1.x DrawerData
    if render:
        off += image_size(d, off)
    if select:
        off += image_size(d, off)
    if deftool:
        n = struct.unpack(">I", d[off:off + 4])[0]
        off += 4 + n
    start = off
    types = []
    if tooltypes:
        count = struct.unpack(">I", d[off:off + 4])[0] // 4 - 1
        off += 4
        for _ in range(count):
            n = struct.unpack(">I", d[off:off + 4])[0]
            types.append(d[off + 4:off + 4 + n].rstrip(b"\0").decode("latin-1"))
            off += 4 + n
    return start, off, types


def build(types):
    out = struct.pack(">I", (len(types) + 1) * 4)
    for t in types:
        b = t.encode("latin-1") + b"\0"
        out += struct.pack(">I", len(b)) + b
    return out


def main():
    path = sys.argv[1]
    d = open(path, "rb").read()
    start, end, types = parse(d)
    if len(sys.argv) == 2:
        print("\n".join(types) if types else "(no tool types)")
        return
    new = sys.argv[2:]
    head = bytearray(d[:start])
    struct.pack_into(">I", head, 54, 0x00010000 if new else 0)   # do_ToolTypes: non-zero = present
    open(path, "wb").write(bytes(head) + (build(new) if new else b"") + d[end:])
    print("tool types:", ", ".join(new))


if __name__ == "__main__":
    main()
