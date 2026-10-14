#!/usr/bin/env python3
"""Build the Workbench icon of AmiDrive: a cloud with arrows up and down, drawn here.

  python tools/make_icon.py build/amiga/AmiDrive.info [--preview icon.png]
                            [--tooltypes "LOCAL=Work:OneDrive" "(WINDOW)" ...]
  python tools/make_icon.py OUT.info --frame picture.png --crop X Y W H    (from a picture)
  python tools/make_icon.py WBStartup.info --project Work:AmiDrive/AmiDrive --tooltypes DONOTWAIT
      a project icon for SYS:WBStartup that starts the program (no second copy of it)

draw_doc() / draw_drawer() and save_icon(..., drawer=True) make the other icons of the
release archive (tools/make_release.py).

The icon has a classic 4 colour image (any Workbench) and an OS 3.5+ ColorIcon (FORM ICON
with FACE and IMAG chunks, RLE compressed), which AmigaOS 3.2 shows. Stack 65536 bytes
(OpenSSL needs it). Copied from the Vastar port's icon tool.
"""

import argparse
import struct

from PIL import Image


def rle_pack(values, depth):
    """ColorIcon RLE: ILBM byterun1 on a bit stream of `depth` bit values"""
    out_bits = []

    def emit(v, n):
        for i in range(n - 1, -1, -1):
            out_bits.append((v >> i) & 1)

    i = 0
    while i < len(values):
        run = 1
        while i + run < len(values) and values[i + run] == values[i] and run < 128:
            run += 1
        if run >= 3:
            emit(257 - run, 8)
            emit(values[i], depth)
            i += run
            continue
        lit = 0
        while i + lit < len(values) and lit < 128:
            r = 1
            while i + lit + r < len(values) and values[i + lit + r] == values[i + lit] and r < 3:
                r += 1
            if r >= 3:
                break
            lit += 1
        emit(lit - 1, 8)
        for v in values[i:i + lit]:
            emit(v, depth)
        i += lit
    while len(out_bits) % 8:
        out_bits.append(0)
    return bytes(int("".join(map(str, out_bits[k:k + 8])), 2) for k in range(0, len(out_bits), 8))


def chunk(name, data):
    pad = b"\0" if len(data) & 1 else b""
    return name + struct.pack(">I", len(data)) + data + pad


def color_icon(idx, w, h, colors):
    depth = max(1, (len(colors) - 1).bit_length())
    pixels = rle_pack(idx, depth)
    palette = b"".join(struct.pack(">BBB", *c) for c in colors)
    face = struct.pack(">BBBBH", w - 1, h - 1, 0, 0x11, len(palette) - 1)
    imag = struct.pack(">BBBBBBHH", 0, len(colors) - 1, 0x02, 1, 0, depth, len(pixels) - 1,
                       len(palette) - 1) + pixels + palette
    body = b"ICON" + chunk(b"FACE", face) + chunk(b"IMAG", imag)
    return b"FORM" + struct.pack(">I", len(body)) + body


# the default Workbench 3.x colours: grey, black, white, blue
WB4 = [(0xAA, 0xAA, 0xAA), (0x00, 0x00, 0x00), (0xFF, 0xFF, 0xFF), (0x66, 0x88, 0xBB)]


def nearest_wb(c):
    return min(range(4), key=lambda i: sum((c[k] - WB4[i][k]) ** 2 for k in range(3)))


def planar(rgb, w, h):
    """the classic 2 bitplane image in the 4 Workbench colours"""
    words = (w + 15) // 16
    planes = []
    for p in range(2):
        data = bytearray()
        for y in range(h):
            bits = 0
            for x in range(words * 16):
                v = nearest_wb(rgb[y * w + x]) if x < w else 0
                bits = (bits << 1) | ((v >> p) & 1)
            data += bits.to_bytes(words * 2, "big")
        planes.append(bytes(data))
    return b"".join(planes)


def tool_types(types):
    out = struct.pack(">I", (len(types) + 1) * 4)
    for t in types:
        b = t.encode("latin-1") + b"\0"
        out += struct.pack(">I", len(b)) + b
    return out


STACK = 65536


def draw(w=46, h=34):
    """the cloud in the 4 Workbench colours plus a lighter blue for the ColorIcon"""
    from PIL import ImageDraw
    grey, black, white, blue = WB4
    img = Image.new("RGB", (w, h), grey)
    d = ImageDraw.Draw(img)
    parts = [(4, 12, 20, 28), (12, 4, 32, 24), (24, 9, 41, 26), (6, 18, 40, 29)]
    for x0, y0, x1, y1 in parts:                    # outline: the parts one pixel bigger
        d.ellipse((x0 - 1, y0 - 1, x1 + 1, y1 + 1), fill=black)
    for x0, y0, x1, y1 in parts:
        d.ellipse((x0, y0, x1, y1), fill=white)
    # arrow down (from the cloud) and up (to the cloud)
    d.rectangle((15, 11, 17, 19), fill=blue)
    d.polygon([(12, 19), (20, 19), (16, 24)], fill=blue)
    d.rectangle((27, 15, 29, 23), fill=blue)
    d.polygon([(24, 15), (32, 15), (28, 10)], fill=blue)
    return img


def draw_doc(w=30, h=34):
    """a text page with a folded corner (documentation)"""
    from PIL import ImageDraw
    grey, black, white, blue = WB4
    img = Image.new("RGB", (w, h), grey)
    d = ImageDraw.Draw(img)
    d.polygon([(3, 1), (19, 1), (26, 8), (26, 32), (3, 32)], fill=white, outline=black)
    d.polygon([(19, 1), (19, 8), (26, 8)], fill=grey, outline=black)
    for y in range(12, 30, 4):
        d.line((7, y, 22, y), fill=blue)
    return img


def draw_installer(w=46, h=40):
    """the installer: a small cloud, an arrow down into a floppy disk"""
    from PIL import ImageDraw
    grey, black, white, blue = WB4
    img = Image.new("RGB", (w, h), grey)
    d = ImageDraw.Draw(img)
    parts = [(2, 7, 13, 18), (7, 2, 21, 15), (15, 5, 27, 17), (3, 11, 26, 19)]
    for x0, y0, x1, y1 in parts:                    # the cloud, upper left
        d.ellipse((x0 - 1, y0 - 1, x1 + 1, y1 + 1), fill=black)
    for x0, y0, x1, y1 in parts:
        d.ellipse((x0, y0, x1, y1), fill=white)
    # the floppy, lower right: body, shutter, label
    d.rectangle((20, 16, 43, 38), fill=blue, outline=black)
    d.rectangle((25, 16, 37, 23), fill=grey, outline=black)
    d.rectangle((33, 17, 35, 22), fill=blue)
    d.rectangle((23, 26, 40, 38), fill=white, outline=black)
    # arrow down on the floppy's label: install
    d.rectangle((30, 27, 32, 31), fill=blue)
    d.polygon([(26, 31), (36, 31), (31, 36)], fill=blue)
    return img


def draw_drawer(w=46, h=30):
    """a drawer (folder) with the cloud's blue"""
    from PIL import ImageDraw
    grey, black, white, blue = WB4
    img = Image.new("RGB", (w, h), grey)
    d = ImageDraw.Draw(img)
    d.rectangle((3, 4, 18, 9), fill=blue, outline=black)
    d.rectangle((3, 8, 42, 27), fill=blue, outline=black)
    d.line((5, 11, 40, 11), fill=white)
    return img


# old DrawerData: struct NewWindow (the drawer window) + dd_CurrentX/Y
DRAWER_DATA = struct.pack(">hhhhBBIIIIIIIhhhhH", 60, 40, 420, 180, 255, 255, 0, 0,
                          0, 0, 0, 0, 0, 90, 40, -1, -1, 1) + struct.pack(">ii", 0, 0)


def disk_object(rgb, w, h, types, default_tool=None, drawer=False):
    """a tool icon, with default_tool a project icon that starts that program, or a drawer"""
    gadget = struct.pack(">IhhhhHHHIIIIIHI", 0, 0, 0, w, h + 1, 0x0004, 0x0003, 0x0001,
                         1, 0, 0, 0, 0, 0, 1)
    kind = 2 if drawer else 4 if default_tool else 3        # WBDRAWER, WBPROJECT, WBTOOL
    head = struct.pack(">HH", 0xE310, 1) + gadget + struct.pack(
        ">BBIIiiIII", kind, 0, 1 if default_tool else 0, 1 if types else 0,
        -0x80000000, -0x80000000, 1 if drawer else 0, 0, 0 if drawer else STACK)
    if drawer:
        head += DRAWER_DATA
    image = struct.pack(">hhhhhIBBI", 0, 0, w, h, 2, 1, 3, 0, 0) + planar(rgb, w, h)
    tool = b""
    if default_tool:
        b = default_tool.encode("latin-1") + b"\0"
        tool = struct.pack(">I", len(b)) + b
    colors = sorted(set(rgb))
    if len(colors) > 256:
        raise SystemExit("more than 256 colours")
    remap = {c: i for i, c in enumerate(colors)}
    tail = struct.pack(">IH", 0, 0) if drawer else b""        # OS 2 dd_Flags, dd_ViewModes
    return head + image + tool + (tool_types(types) if types else b"") + tail + \
        color_icon([remap[c] for c in rgb], w, h, colors)


def save_icon(path, img, types=(), default_tool=None, drawer=False):
    w, h = img.size
    rgb = list(img.convert("RGB").getdata())
    open(path, "wb").write(disk_object(rgb, w, h, list(types), default_tool, drawer))


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("out")
    ap.add_argument("--frame", help="a picture instead of the drawn cloud")
    ap.add_argument("--crop", nargs=4, type=int, metavar=("X", "Y", "W", "H"))
    ap.add_argument("--preview")
    ap.add_argument("--tooltypes", nargs="*", default=[])
    ap.add_argument("--project", metavar="PROGRAM",
                    help="project icon that starts PROGRAM (e.g. in SYS:WBStartup)")
    args = ap.parse_args()
    if args.frame:
        x, y, w, h = args.crop
        img = Image.open(args.frame).convert("RGB").crop((x, y, x + w, y + h))
    else:
        img = draw()
        w, h = img.size
    rgb = list(img.getdata())
    open(args.out, "wb").write(disk_object(rgb, w, h, args.tooltypes, args.project))
    if args.preview:
        img.resize((w * 6, h * 6), Image.NEAREST).save(args.preview)
    print(f"{args.out}: {w}x{h}, {len(set(rgb))} colours, tool types {args.tooltypes}")


if __name__ == "__main__":
    main()
