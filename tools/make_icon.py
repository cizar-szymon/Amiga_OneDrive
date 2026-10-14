#!/usr/bin/env python3
"""Workbench icons of AmiDrive in the GlowIcons style of AmigaOS 3.5+ (like the Warp
installer icons): 46x46, shaded colours, transparent background, a second image with a
yellow glow when selected, plus a classic 4 colour image for older Workbenches.

  python tools/make_icon.py build/amiga/AmiDrive.info [--art cloud|installer|drawer|doc]
                            [--preview icon.png] [--tooltypes "LOCAL=Work:OneDrive" ...]
  python tools/make_icon.py WBStartup.info --project Work:AmiDrive/AmiDrive --tooltypes DONOTWAIT
      a project icon for SYS:WBStartup that starts the program (no second copy of it)

save_icon(path, art, ...) is used by tools/make_release.py for the archive icons.

The pictures are drawn at 4x size with gradients and scaled down (anti-aliased), then
reduced to at most 31 colours + transparent per image (ColorIcon: FORM ICON with FACE and
two IMAG chunks, RLE compressed). Stack 65536 bytes (OpenSSL needs it).
"""

import argparse
import struct

from PIL import Image, ImageChops, ImageDraw, ImageFilter

SIZE = 46                       # GlowIcons size
SS = 4                          # drawn at 4x, then scaled down
STACK = 65536

# the default Workbench 3.x colours: grey, black, white, blue
WB4 = [(0xAA, 0xAA, 0xAA), (0x00, 0x00, 0x00), (0xFF, 0xFF, 0xFF), (0x66, 0x88, 0xBB)]


# ---- ColorIcon / DiskObject writing ----------------------------------------------------

def rle_pack(values, depth):
    """ColorIcon RLE: 8 bit control codes and `depth` bit values in one bit stream"""
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


def indexed(rgba):
    """RGBA -> (indices, palette): index 0 transparent, at most 31 colours"""
    w, h = rgba.size
    alpha = rgba.getchannel("A")
    rgb = Image.new("RGB", (w, h), WB4[0])
    rgb.paste(rgba.convert("RGB"), mask=alpha)
    q = rgb.quantize(colors=31, method=Image.Quantize.MEDIANCUT, dither=Image.Dither.NONE)
    pal = q.getpalette()[:31 * 3]
    colors = [WB4[0]] + [tuple(pal[i:i + 3]) for i in range(0, len(pal), 3)]
    a = list(alpha.getdata())
    idx = [0 if a[k] < 128 else v + 1 for k, v in enumerate(q.getdata())]
    used = sorted(set(idx) | {0})                      # drop unused palette entries
    remap = {v: n for n, v in enumerate(used)}
    return [remap[v] for v in idx], [colors[v] for v in used]


def color_icon(frames, w, h):
    """frames: [(indices, palette)], index 0 transparent"""
    maxpal = max(len(p) for _, p in frames) * 3
    face = struct.pack(">BBBBH", w - 1, h - 1, 0, 0x11, maxpal - 1)
    body = b"ICON" + chunk(b"FACE", face)
    for idx, colors in frames:
        depth = max(1, (len(colors) - 1).bit_length())
        pixels = rle_pack(idx, depth)
        palette = b"".join(struct.pack(">BBB", *c) for c in colors)
        imag = struct.pack(">BBBBBBHH", 0, len(colors) - 1, 0x03, 1, 0, depth, len(pixels) - 1,
                           len(palette) - 1) + pixels + palette
        body += chunk(b"IMAG", imag)
    return b"FORM" + struct.pack(">I", len(body)) + body


def nearest_wb(c):
    return min(range(4), key=lambda i: sum((c[k] - WB4[i][k]) ** 2 for k in range(3)))


def planar(rgba):
    """the classic 2 bitplane image in the 4 Workbench colours (transparent = grey)"""
    w, h = rgba.size
    px = list(rgba.getdata())
    words = (w + 15) // 16
    planes = []
    for p in range(2):
        data = bytearray()
        for y in range(h):
            bits = 0
            for x in range(words * 16):
                v = nearest_wb(px[y * w + x][:3]) if x < w and px[y * w + x][3] >= 128 else 0
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


# old DrawerData: struct NewWindow (the drawer window) + dd_CurrentX/Y
DRAWER_DATA = struct.pack(">hhhhBBIIIIIIIhhhhH", 60, 40, 420, 180, 255, 255, 0, 0,
                          0, 0, 0, 0, 0, 90, 40, -1, -1, 1) + struct.pack(">ii", 0, 0)


def disk_object(normal, selected, types, default_tool=None, drawer=False):
    """a tool icon, with default_tool a project icon that starts that program, or a drawer"""
    w, h = normal.size
    gadget = struct.pack(">IhhhhHHHIIIIIHI", 0, 0, 0, w, h + 1, 0x0004, 0x0003, 0x0001,
                         1, 0, 0, 0, 0, 0, 1)
    kind = 2 if drawer else 4 if default_tool else 3        # WBDRAWER, WBPROJECT, WBTOOL
    head = struct.pack(">HH", 0xE310, 1) + gadget + struct.pack(
        ">BBIIiiIII", kind, 0, 1 if default_tool else 0, 1 if types else 0,
        -0x80000000, -0x80000000, 1 if drawer else 0, 0, 0 if drawer else STACK)
    if drawer:
        head += DRAWER_DATA
    image = struct.pack(">hhhhhIBBI", 0, 0, w, h, 2, 1, 3, 0, 0) + planar(normal)
    tool = b""
    if default_tool:
        b = default_tool.encode("latin-1") + b"\0"
        tool = struct.pack(">I", len(b)) + b
    tail = struct.pack(">IH", 0, 0) if drawer else b""        # OS 2 dd_Flags, dd_ViewModes
    return head + image + tool + (tool_types(types) if types else b"") + tail + \
        color_icon([indexed(normal), indexed(selected)], w, h)


# ---- drawing helpers (coordinates in icon pixels, drawn at SS times) ---------------------

def canvas():
    return Image.new("RGBA", (SIZE * SS, SIZE * SS), (0, 0, 0, 0))


def s(*v):
    return [round(x * SS) for x in v]


def gradient(size, top, bottom, horizontal=False):
    w, h = size
    g = Image.new("RGBA", (w, h))
    d = ImageDraw.Draw(g)
    n = w if horizontal else h
    for i in range(n):
        t = i / max(1, n - 1)
        c = tuple(round(top[k] + (bottom[k] - top[k]) * t) for k in range(3)) + (255,)
        if horizontal:
            d.line((i, 0, i, h), fill=c)
        else:
            d.line((0, i, w, i), fill=c)
    return g


def fill_shape(img, mask, top, bottom, outline=None, width=1.0, horizontal=False):
    """paint mask (L, full canvas) with a gradient over its box and an outline"""
    box = mask.getbbox()
    if not box:
        return
    g = Image.new("RGBA", img.size, (0, 0, 0, 0))
    g.paste(gradient((box[2] - box[0], box[3] - box[1]), top, bottom, horizontal), box[:2])
    if outline:
        edge = ImageChops.subtract(mask, mask.filter(ImageFilter.MinFilter(int(width * SS) | 1)))
        o = Image.new("RGBA", img.size, outline + (255,))
        img.paste(g, mask=mask)
        img.paste(o, mask=edge)
    else:
        img.paste(g, mask=mask)


def shadow(img, mask, dx=1.5, dy=1.5, alpha=110):
    sh = Image.new("RGBA", img.size, (20, 20, 40, 0))
    m = ImageChops.offset(mask, round(dx * SS), round(dy * SS)).filter(ImageFilter.GaussianBlur(SS))
    sh.putalpha(m.point(lambda v: v * alpha // 255))
    img.alpha_composite(sh)


def mask_of(draw_fn):
    m = Image.new("L", (SIZE * SS, SIZE * SS), 0)
    draw_fn(ImageDraw.Draw(m))
    return m


def cloud_mask(x, y, sc):
    """a cloud in a box from (x, y), about 40*sc x 26*sc"""
    def f(d):
        for cx, cy, r in ((11, 16, 8), (20, 11, 10.5), (30, 14, 8.5), (36, 19, 5.5)):
            d.ellipse(s(x + (cx - r) * sc, y + (cy - r) * sc, x + (cx + r) * sc, y + (cy + r) * sc), fill=255)
        d.rounded_rectangle(s(x + 4 * sc, y + 14 * sc, x + 41 * sc, y + 24 * sc), radius=5 * sc * SS, fill=255)
    return mask_of(f)


def draw_cloud(img, x, y, sc):
    m = cloud_mask(x, y, sc)
    shadow(img, m)
    fill_shape(img, m, (255, 255, 255), (186, 214, 242), outline=(31, 78, 140), width=1.1)
    # light on the upper left
    hl = mask_of(lambda d: d.ellipse(s(x + 13 * sc, y + 3 * sc, x + 25 * sc, y + 9 * sc), fill=150))
    img.paste(Image.new("RGBA", img.size, (255, 255, 255, 255)), mask=hl.filter(ImageFilter.GaussianBlur(SS)))


def arrow(img, cx, top, bottom, width, down, top_col, bottom_col):
    """a fat arrow with gradient and dark outline"""
    hw, sw = width / 2, width / 4
    if down:
        pts = [(cx - sw, top), (cx + sw, top), (cx + sw, bottom - hw), (cx + hw, bottom - hw),
               (cx, bottom), (cx - hw, bottom - hw), (cx - sw, bottom - hw)]
    else:
        pts = [(cx, top), (cx + hw, top + hw), (cx + sw, top + hw), (cx + sw, bottom),
               (cx - sw, bottom), (cx - sw, top + hw), (cx - hw, top + hw)]
    m = mask_of(lambda d: d.polygon([tuple(s(px, py)) for px, py in pts], fill=255))
    fill_shape(img, m, top_col, bottom_col, outline=(16, 40, 80), width=0.9)


def finish(img):
    small = img.resize((SIZE, SIZE), Image.LANCZOS)
    a = small.getchannel("A").point(lambda v: 255 if v >= 110 else 0)
    small.putalpha(a)
    return small


def glow(icon):
    """the selected image: a yellow glow around the picture, like the GlowIcons"""
    a = icon.getchannel("A")
    out = Image.new("RGBA", icon.size, (0, 0, 0, 0))
    rings = [(3, (255, 196, 30)), (2, (255, 222, 60)), (1, (255, 246, 120))]
    for r, col in rings:
        grown = a.filter(ImageFilter.MaxFilter(2 * r + 1))
        layer = Image.new("RGBA", icon.size, col + (255,))
        if r == 3:          # the outer ring dithered, as in the Warp icons
            dith = Image.new("L", icon.size, 0)
            dith.putdata([255 if (x + y) % 2 == 0 else 0 for y in range(icon.size[1]) for x in range(icon.size[0])])
            grown = ImageChops.multiply(grown, dith)
        out.paste(layer, mask=grown)
    brighter = icon.point(lambda v: min(255, v + 18))
    brighter.putalpha(a)
    out.alpha_composite(brighter)
    return out


# ---- the pictures --------------------------------------------------------------------

def art_cloud():
    """AmiDrive: the cloud with an arrow down (blue) and up (green)"""
    img = canvas()
    draw_cloud(img, 2, 8, 1.0)
    arrow(img, 17.5, 14, 30, 9, True, (90, 170, 245), (0, 90, 200))
    arrow(img, 28.5, 13, 29, 9, False, (110, 215, 120), (20, 140, 60))
    return finish(img)


def art_installer():
    """the installer, like the Warp one: a drive, the cloud, a green round arrow down"""
    img = canvas()
    # drive: light top, dark front with slot and LED
    body = mask_of(lambda d: d.rounded_rectangle(s(3, 22, 43, 42), radius=2 * SS, fill=255))
    shadow(img, body)
    fill_shape(img, body, (235, 235, 235), (120, 120, 125), outline=(40, 40, 45), width=1)
    front = mask_of(lambda d: d.rectangle(s(5, 31, 41, 40), fill=255))
    fill_shape(img, front, (70, 70, 75), (25, 25, 30))
    d = ImageDraw.Draw(img)
    for x in range(27, 39, 2):
        d.rectangle(s(x, 33, x + 0.8, 38.5), fill=(170, 170, 175, 255))
    d.ellipse(s(8, 34.5, 10.8, 37.3), fill=(255, 150, 20, 255))
    d.rectangle(s(13, 34.5, 24, 37), fill=(15, 15, 18, 255))
    # the cloud on the drive, right
    draw_cloud(img, 16, 3, 0.7)
    # green round button with a white arrow down (upper left)
    circ = mask_of(lambda d: d.ellipse(s(2, 2, 22, 22), fill=255))
    shadow(img, circ, 1, 1)
    halo = mask_of(lambda d: d.ellipse(s(0.8, 0.8, 23.2, 23.2), fill=255))
    img.paste(Image.new("RGBA", img.size, (255, 255, 255, 255)), mask=halo)
    fill_shape(img, circ, (90, 200, 140), (20, 120, 75), outline=(10, 70, 40), width=0.8)
    arrow(img, 12, 6, 18.5, 9, True, (255, 255, 255), (215, 225, 220))
    return finish(img)


def art_drawer():
    """a blue drawer (like the Warp one) with a small cloud"""
    img = canvas()
    box = mask_of(lambda d: d.polygon([tuple(s(4, 18)), tuple(s(10, 8)), tuple(s(38, 8)),
                                       tuple(s(43, 18)), tuple(s(43, 40)), tuple(s(4, 40))], fill=255))
    shadow(img, box)
    fill_shape(img, box, (110, 155, 210), (35, 75, 140), outline=(15, 35, 75), width=1)
    top = mask_of(lambda d: d.polygon([tuple(s(6, 17)), tuple(s(11, 10)), tuple(s(37, 10)),
                                       tuple(s(41, 17))], fill=255))
    fill_shape(img, top, (40, 70, 120), (20, 45, 90))
    d = ImageDraw.Draw(img)
    d.line(s(5, 27, 42, 27), fill=(15, 35, 75, 255), width=SS)
    d.line(s(5, 28, 42, 28), fill=(140, 180, 230, 255), width=SS // 2)
    handle = mask_of(lambda d: d.rounded_rectangle(s(17, 31, 30, 35), radius=SS, fill=255))
    fill_shape(img, handle, (240, 240, 240), (140, 140, 145), outline=(40, 40, 45), width=0.8)
    draw_cloud(img, 18, 1, 0.6)
    return finish(img)


def art_doc():
    """a page with a folded corner, text lines and a small cloud"""
    img = canvas()
    page = mask_of(lambda d: d.polygon([tuple(s(8, 3)), tuple(s(30, 3)), tuple(s(39, 12)),
                                        tuple(s(39, 43)), tuple(s(8, 43))], fill=255))
    shadow(img, page)
    fill_shape(img, page, (255, 255, 255), (215, 220, 228), outline=(50, 55, 70), width=1)
    corner = mask_of(lambda d: d.polygon([tuple(s(30, 3)), tuple(s(30, 12)), tuple(s(39, 12))], fill=255))
    fill_shape(img, corner, (200, 205, 215), (150, 155, 170), outline=(50, 55, 70), width=0.8)
    d = ImageDraw.Draw(img)
    for i, y in enumerate(range(22, 41, 4)):
        d.line(s(12, y, 35 - (6 if i == 4 else 0), y), fill=(60, 110, 190, 255), width=round(1.4 * SS))
    draw_cloud(img, 10, 6, 0.42)
    return finish(img)


ARTS = {"cloud": art_cloud, "installer": art_installer, "drawer": art_drawer, "doc": art_doc}


def save_icon(path, art, types=(), default_tool=None, drawer=False):
    img = ARTS[art]()
    open(path, "wb").write(disk_object(img, glow(img), list(types), default_tool, drawer))
    return img


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("out")
    ap.add_argument("--art", choices=sorted(ARTS), default="cloud")
    ap.add_argument("--preview")
    ap.add_argument("--tooltypes", nargs="*", default=[])
    ap.add_argument("--project", metavar="PROGRAM",
                    help="project icon that starts PROGRAM (e.g. in SYS:WBStartup)")
    args = ap.parse_args()
    img = save_icon(args.out, args.art, args.tooltypes, args.project, args.art == "drawer")
    if args.preview:
        both = Image.new("RGBA", (SIZE * 2 + 4, SIZE), WB4[0] + (255,))
        both.alpha_composite(img, (0, 0))
        both.alpha_composite(glow(img), (SIZE + 4, 0))
        both.resize((both.width * 6, both.height * 6), Image.NEAREST).save(args.preview)
    print(f"{args.out}: {args.art}, tool types {args.tooltypes}")


if __name__ == "__main__":
    main()
