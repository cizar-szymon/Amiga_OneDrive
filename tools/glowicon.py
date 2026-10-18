#!/usr/bin/env python3
"""Read and write GlowIcons (AmigaOS 3.5+ ColorIcon: FORM ICON with FACE and IMAG chunks),
and build the TempSD disk icon of the A1200 in the style of its SystemSD / WorkSD icons.

  python tools/glowicon.py show X.info PREFIX [--scale 4]
      both images as PREFIX_0.png (normal) and PREFIX_1.png (selected)
  python tools/glowicon.py tempsd SystemSD.info WorkSD.info TempSD.info OUT.info [--preview P.png]
      the hard disk of SystemSD/WorkSD without their emblems, with an hourglass ("temporary");
      the selected image darker with a yellow glow. The header of TempSD.info (place on the
      Workbench, drawer window) is kept, only its FORM ICON is replaced.

The icons come from the Amiga (not in the repository: they belong to an icon set):
  python tools/amiga_remote.py get SystemSD:Disk.info SystemSD.info   (WorkSD, TempSD alike)
Back on the Amiga: Copy TempSD:Disk.info TempSD:Disk.info.bak, put OUT.info TempSD:Disk.info;
the Workbench shows a new disk icon after a restart (it reads it when the volume is mounted).

Both images are 46x46. Writing uses RLE (control byte n-1 literal / 257-n repeat, values
of <depth> bits); every written icon is read back and compared.
"""

import argparse
import struct
import sys

from PIL import Image

W = H = 46                              # GlowIcons size


# ---- reading ---------------------------------------------------------------------

def decode_rle(data, depth, count):
    pos = 0

    def get(n):
        nonlocal pos
        v = 0
        for _ in range(n):
            v = (v << 1) | (data[pos >> 3] >> (7 - (pos & 7))) & 1
            pos += 1
        return v

    out = []
    while len(out) < count and pos + 8 <= len(data) * 8:
        c = get(8)
        if c < 128:
            out += [get(depth) for _ in range(c + 1)]
        elif c > 128:
            out += [get(depth)] * (257 - c)
    return out[:count]


def find_form(d):
    i = d.find(b"FORM")
    while i >= 0 and d[i + 8:i + 12] != b"ICON":
        i = d.find(b"FORM", i + 1)
    return i


def read(path):
    """(width, height, [image, image]); an image is a list of (r, g, b) or None (transparent)"""
    d = open(path, "rb").read()
    i = find_form(d)
    if i < 0:
        raise SystemExit(f"{path}: no FORM ICON (not a GlowIcon)")
    p, end = i + 12, i + 8 + struct.unpack(">I", d[i + 4:i + 8])[0]
    w = h = 0
    imgs, pal = [], None
    while p < end:
        cid, clen = d[p:p + 4], struct.unpack(">I", d[p + 4:p + 8])[0]
        c = d[p + 8:p + 8 + clen]
        if cid == b"FACE":
            w, h = c[0] + 1, c[1] + 1
        elif cid == b"IMAG":
            trans, ncol, flags, ifmt, pfmt, depth = c[0], c[1] + 1, c[2], c[3], c[4], c[5]
            nimg = struct.unpack(">H", c[6:8])[0] + 1
            npal = struct.unpack(">H", c[8:10])[0] + 1
            raw = c[10:10 + nimg]
            pix = decode_rle(raw, depth, w * h) if ifmt == 1 else list(raw[:w * h])
            if flags & 2:                       # own palette (else the one of the first image)
                praw = c[10 + nimg:10 + nimg + npal]
                if pfmt == 1:
                    praw = bytes(decode_rle(praw, 8, ncol * 3))
                pal = [tuple(praw[k * 3:k * 3 + 3]) for k in range(ncol)]
            imgs.append([None if (flags & 1 and v == trans) else
                         (pal[v] if v < len(pal) else (255, 0, 255)) for v in pix])
        p += 8 + clen + (clen & 1)
    return w, h, imgs


def to_png(w, h, px, path, scale=1, bg=None):
    im = Image.new("RGBA", (w, h), (0, 0, 0, 0))
    for i, c in enumerate(px):
        if c is not None:
            im.putpixel((i % w, i // w), c + (255,))
    im = im.resize((w * scale, h * scale), Image.NEAREST)
    if bg:
        back = Image.new("RGBA", im.size, bg + (255,))
        back.paste(im, (0, 0), im)
        im = back
    im.save(path)


# ---- writing ---------------------------------------------------------------------

def encode_rle(values, depth):
    bits = []

    def put(v, n):
        bits.extend((v >> b) & 1 for b in range(n - 1, -1, -1))

    i = 0
    while i < len(values):
        run = 1
        while i + run < len(values) and values[i + run] == values[i] and run < 128:
            run += 1
        if run >= 3:
            put(257 - run, 8)
            put(values[i], depth)
            i += run
            continue
        lit = []
        while i < len(values) and len(lit) < 128:
            r = 1
            while i + r < len(values) and values[i + r] == values[i] and r < 3:
                r += 1
            if r >= 3:
                break
            lit.append(values[i])
            i += 1
        put(len(lit) - 1, 8)
        for v in lit:
            put(v, depth)
    bits += [0] * (-len(bits) % 8)
    return bytes(int("".join(map(str, bits[k:k + 8])), 2) for k in range(0, len(bits), 8))


def imag_chunk(px):
    """IMAG with its own palette; index 0 is transparent"""
    pal, idx = [None], []
    for c in px:
        if c is None:
            idx.append(0)
            continue
        if c not in pal:
            pal.append(c)
        idx.append(pal.index(c))
    if len(pal) > 256:
        raise SystemExit("more than 255 colours in an image")
    pal[0] = (0, 0, 0)
    depth = max(1, (len(pal) - 1).bit_length())
    data = encode_rle(idx, depth)
    assert decode_rle(data, depth, len(idx)) == idx, "RLE round trip"
    palb = b"".join(bytes(c) for c in pal)
    body = (bytes([0, len(pal) - 1, 3, 1, 0, depth]) +   # transparent 0, own palette, RLE
            struct.pack(">HH", len(data) - 1, len(palb) - 1) + data + palb)
    return b"IMAG" + struct.pack(">I", len(body)) + body + b"\0" * (len(body) & 1), len(pal)


def write(header_path, images, out, w=W, h=H):
    """the .info of header_path with its FORM ICON replaced by these images"""
    chunks, most = b"", 0
    for px in images:
        c, n = imag_chunk(px)
        chunks += c
        most = max(most, n)
    face = bytes([w - 1, h - 1, 0, 0]) + struct.pack(">H", most * 3 - 1)
    body = b"ICON" + b"FACE" + struct.pack(">I", len(face)) + face + chunks
    d = open(header_path, "rb").read()
    i = find_form(d)
    if i < 0:
        raise SystemExit(f"{header_path}: no FORM ICON to replace")
    size = struct.unpack(">I", d[i + 4:i + 8])[0]
    if d[i + 8 + size + (size & 1):].strip(b"\0"):
        raise SystemExit(f"{header_path}: data after the FORM ICON")
    open(out, "wb").write(d[:i] + b"FORM" + struct.pack(">I", len(body)) + body)
    if read(out)[2] != list(images):
        raise SystemExit(f"{out}: read back differs")


# ---- the TempSD icon -------------------------------------------------------------

HOURGLASS = [
    "KKKKKKKKKKKKK",
    "KLLLLLLLLLLBK",
    "KBBBBBBBBBBBK",
    ".BgggggggggB.",
    ".BgWGGGGGGgB.",
    ".BgWGGGGGGgB.",
    ".BgGYYYYYYgB.",
    ".B.gOYYYYg.B.",
    ".B..gOYYg..B.",
    ".B...gYg...B.",
    ".B...gYg...B.",
    ".B..gGYGg..B.",
    ".B.gWGYGGg.B.",
    ".BgWGGYGGGgB.",
    ".BgGGYYYGGgB.",
    ".BgGYYYYYGgB.",
    ".BgYYYYYYOgB.",
    ".BgggggggggB.",
    "KBBBBBBBBBBBK",
    "KLLLLLLLLLLBK",
    "KKKKKKKKKKKKK",
]
HG_COLORS = {
    "K": (90, 50, 20), "B": (160, 95, 40), "L": (215, 155, 85),          # wooden frame
    "g": (70, 100, 140), "G": (200, 225, 242), "W": (255, 255, 255),     # glass
    "Y": (255, 205, 40), "O": (230, 140, 0),                             # sand
}
HG_X, HG_Y = 25, 2                      # standing on the right of the disk top
GLOW = {1: (255, 255, 255), 2: (239, 231, 20), 3: (223, 186, 69)}       # as in the originals
DIM = 0.69                              # the selected image is this much darker
TOP = 17                                # first row of the disk; above it only emblems


def gray(c, tol=14):
    return c is not None and max(c) - min(c) <= tol


def clean_disk(a, b):
    """the disk of two icons with different emblems: equal pixels kept, the top rebuilt
    from grey pixels (also mirrored), dark remains on the top replaced by its dither"""
    out = [None] * (W * H)
    for y in range(TOP, H):
        for x in range(W):
            i, m = y * W + x, y * W + (W - 1 - x)
            if a[i] == b[i]:
                out[i] = a[i]
                continue
            for c in (a[i], a[m] if a[m] == b[m] else None, a[m], b[i]):
                if gray(c):
                    out[i] = c
                    break
    for y in range(TOP, TOP + 10):          # holes inside the outline: the grey next to them
        for x in range(W):
            i = y * W + x
            if out[i] is None and a[i] is not None and b[i] is not None:
                for dx in (1, -1, 2, -2):
                    if gray(out[i + dx]):
                        out[i] = out[i + dx]
                        break
    for y in range(TOP, TOP + 9):           # the top surface, the row ends stay
        xs = [x for x in range(W) if out[y * W + x] is not None]
        for x in range(xs[0] + 2, xs[-1] - 1) if xs else ():
            i = y * W + x
            if out[i] is not None and sum(out[i]) // 3 <= 127 and out[i - 2] is not None:
                out[i] = out[i - 2]         # same phase of the dither pattern
    return out


def dim(c, f=DIM):
    return tuple(int(v * f) for v in c)


def tempsd(system, work):
    """normal and selected image of the TempSD icon"""
    n = clean_disk(system[0], work[0])
    s = clean_disk(system[1], work[1])
    sel = [None] * (W * H)
    for i in range(W * H):
        if n[i] is not None:                # the selected disk of the originals, else darker
            sel[i] = s[i] if (s[i] is not None and (gray(s[i], 40) or i // W > TOP + 9)) else dim(n[i])
    foot = HG_Y + len(HOURGLASS)
    for y in range(foot - 3, foot + 1):     # a soft shadow left of the hourglass foot
        for x in range(HG_X - 3, HG_X):
            i = y * W + x
            if n[i] is not None and (x + y) % 2 == 0:
                n[i], sel[i] = dim(n[i], 0.7), dim(sel[i], 0.7)
    for y, row in enumerate(HOURGLASS):
        for x, ch in enumerate(row):
            if ch != ".":
                i = (HG_Y + y) * W + HG_X + x
                n[i], sel[i] = HG_COLORS[ch], dim(HG_COLORS[ch])
    for i in range(W * H):                  # the glow around the whole shape
        if n[i] is not None:
            continue
        x, y = i % W, i // W
        d = min((max(abs(xx - x), abs(yy - y))
                 for yy in range(max(0, y - 3), min(H, y + 4))
                 for xx in range(max(0, x - 3), min(W, x + 4)) if n[yy * W + xx] is not None),
                default=9)
        if d in (1, 2) or (d == 3 and (x + y) % 2 == 0):   # the outer ring dotted
            sel[i] = GLOW[d]
    return n, sel


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = ap.add_subparsers(dest="cmd", required=True)
    s = sub.add_parser("show")
    s.add_argument("info")
    s.add_argument("prefix")
    s.add_argument("--scale", type=int, default=4)
    t = sub.add_parser("tempsd")
    t.add_argument("system")
    t.add_argument("work")
    t.add_argument("header", help="the TempSD.info to take the header from")
    t.add_argument("out")
    t.add_argument("--preview")
    a = ap.parse_args()

    if a.cmd == "show":
        w, h, imgs = read(a.info)
        for k, px in enumerate(imgs):
            to_png(w, h, px, f"{a.prefix}_{k}.png", a.scale)
        print(a.info, f"{w}x{h}", len(imgs), "images")
    else:
        sys_imgs, work_imgs = read(a.system)[2], read(a.work)[2]
        n, sel = tempsd(sys_imgs, work_imgs)
        write(a.header, [n, sel], a.out)
        if a.preview:
            to_png(W * 2 + 2, H, [n[y * W + x] if x < W else (sel[y * W + x - W - 2] if x >= W + 2 else None)
                                  for y in range(H) for x in range(W * 2 + 2)],
                   a.preview, 6, (160, 160, 160))
        print(a.out)


if __name__ == "__main__":
    main()
