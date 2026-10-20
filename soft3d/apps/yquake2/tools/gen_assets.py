#!/usr/bin/env python3
"""
gen_assets.py - generate the freely-redistributable assets the standalone
yquake2 gl3 refresher port needs to render a scene (Phase E of the port plan).

Nothing here is proprietary Quake2 data.  Three files are produced under res/:

  res/pics/colormap.pcx        8bpp PCX with a valid trailing 256-entry palette.
                               REQUIRED: GetPCXPalette() fatally errors (ri.Sys_Error
                               ERR_FATAL) if pics/colormap.pcx is missing/invalid, and
                               GL3_Init() calls it unconditionally.  Only the palette is
                               consumed (d_8to24table); pixel content is irrelevant.

  res/models/probe/skin.pcx    32bpp (RGBA) PCX texture used by the probe model.
                               32bpp bypasses the 8bpp colormap/flood-fill path in
                               gl3_image.c, so the skin is self-contained.

  res/models/probe/tris.md2    a procedural textured cube (1 frame, 24 verts, 6 faces
                               emitted as triangle strips in the glcmds list).  Per-face
                               lightnormalindex values are chosen from the refresher's own
                               r_avertexnormals table (constants/anorms.h) so the
                               RF_FULLBRIGHT normal shading lights each face correctly.

The PCX/MD2 byte layouts mirror upstream/src/client/refresh/files/pcx.c and
upstream/src/common/header/files.h exactly.
"""

import os
import re
import struct

HERE = os.path.dirname(os.path.abspath(__file__))
APP = os.path.dirname(HERE)
RES = os.path.join(APP, "res")
ANORMS = os.path.join(APP, "upstream", "src", "client", "refresh",
                      "constants", "anorms.h")


# --------------------------------------------------------------------------
# PCX helpers (header layout from common/header/files.h: 128 bytes)
# --------------------------------------------------------------------------
def rle_encode(row):
    """Standard PCX RLE: runs of 2..63 -> 0xC0|count, value; literals that
    would look like a run byte are escaped as 0xC1, value."""
    out = bytearray()
    i = 0
    n = len(row)
    while i < n:
        val = row[i]
        run = 1
        while run < 63 and i + run < n and row[i + run] == val:
            run += 1
        if run >= 2:
            out.append(0xC0 | run)
            out.append(val)
            i += run
        else:
            if (val & 0xC0) == 0xC0:
                out.append(0xC1)
            out.append(val)
            i += 1
    return bytes(out)


def pcx_header(width, height, color_planes, bits_per_pixel, bytes_per_line):
    h = bytearray(128)
    h[0] = 0x0A          # manufacturer
    h[1] = 5             # version
    h[2] = 1             # encoding (RLE)
    h[3] = bits_per_pixel
    struct.pack_into("<HHHH", h, 4, 0, 0, width - 1, height - 1)  # xmin,ymin,xmax,ymax
    struct.pack_into("<HH", h, 12, 72, 72)                        # hres, vres
    # h[16:64] 48-byte EGA palette (zeros fine)
    h[64] = 0            # reserved
    h[65] = color_planes
    struct.pack_into("<H", h, 66, bytes_per_line)
    struct.pack_into("<H", h, 68, 1)         # palette_type (colour)
    # h[70:128] filler (zeros)
    return bytes(h)


def write_colormap(path):
    """16x16 8bpp PCX with a rainbow 256-entry trailing palette."""
    w = hgt = 16
    planes = 1
    bpl = w            # must be > pcx_width (= w-1)
    body = bytearray()
    row = bytes(w)     # all index 0
    enc = rle_encode(row)
    for _ in range(hgt):
        body += enc
    pal = bytearray()
    for i in range(256):
        # spread a smooth rainbow; index 255 is treated as transparent upstream
        r = (i * 3) & 0xFF
        g = (i * 5 + 80) & 0xFF
        b = (i * 7 + 160) & 0xFF
        pal += bytes((r, g, b))
    data = pcx_header(w, hgt, planes, 8, bpl) + bytes(body) + b"\x0C" + bytes(pal)
    with open(path, "wb") as f:
        f.write(data)
    return len(data)


def write_skin(path, w=64, hgt=64):
    """32bpp RGBA PCX: a two-tone checker with a border, so the cube faces are
    visually distinct.  Planes are stored per row as [R*w][G*w][B*w][A*w]."""
    planes = 4
    bpl = w
    body = bytearray()
    for y in range(hgt):
        rp = bytearray(); gp = bytearray(); bp = bytearray(); ap = bytearray()
        for x in range(w):
            edge = (x == 0 or y == 0 or x == w - 1 or y == hgt - 1)
            checker = ((x // 8) + (y // 8)) % 2 == 0
            if edge:
                r, g, b = 40, 40, 40
            elif checker:
                r, g, b = 200, 120, 60
            else:
                r, g, b = 70, 130, 200
            rp.append(r); gp.append(g); bp.append(b); ap.append(255)
        row = bytes(rp) + bytes(gp) + bytes(bp) + bytes(ap)
        body += rle_encode(row)
    data = pcx_header(w, hgt, planes, 8, bpl) + bytes(body)
    with open(path, "wb") as f:
        f.write(data)
    return len(data), w, hgt


# --------------------------------------------------------------------------
# MD2 (alias) helper - layout from common/header/files.h dmdl_t
# --------------------------------------------------------------------------
def load_anorms():
    txt = open(ANORMS, "r").read()
    vals = re.findall(r"\{\s*(-?[\d.]+)\s*,\s*(-?[\d.]+)\s*,\s*(-?[\d.]+)\s*\}", txt)
    return [(float(a), float(b), float(c)) for (a, b, c) in vals]


def nearest_anorm(anorms, n):
    best = 0; bestd = -2.0
    for i, a in enumerate(anorms):
        d = a[0] * n[0] + a[1] * n[1] + a[2] * n[2]
        if d > bestd:
            bestd = d; best = i
    return best


def write_md2(path, skin_name="models/probe/skin.pcx", skin_w=64, skin_h=64):
    anorms = load_anorms()
    H = 32.0
    # face: (fixed_axis, fixed_sign, u_axis, v_axis, outward_normal)
    faces = [
        (0, +1, 1, 2, (1, 0, 0)),    # +X
        (0, -1, 1, 2, (-1, 0, 0)),   # -X
        (1, +1, 0, 2, (0, 1, 0)),    # +Y
        (1, -1, 0, 2, (0, -1, 0)),   # -Y
        (2, +1, 0, 1, (0, 0, 1)),    # +Z
        (2, -1, 0, 1, (0, 0, -1)),   # -Z
    ]
    signs = [-1.0, +1.0]  # for u,v = -H,+H

    verts = []       # list of (x,y,z) float per vertex, 24 total
    lni = []         # lightnormalindex per vertex
    glcmds = []      # int list
    tris = []        # (index_xyz[3], index_st[3]) for the (unused-by-gl) tri table

    for f, (fa, fs, ua, va, nrm) in enumerate(faces):
        ln = nearest_anorm(anorms, nrm)
        base = len(verts)
        corner_pos = []
        # c0=(u-,v-), c1=(u-,v+), c2=(u+,v-), c3=(u+,v+)
        order_uv = [(0, 0), (0, 1), (1, 0), (1, 1)]  # c0,c1,c2,c3
        for (uu, vv) in order_uv:
            p = [0.0, 0.0, 0.0]
            p[fa] = fs * H
            p[ua] = signs[uu]
            p[va] = signs[vv]
            corner_pos.append(tuple(p))
        for p in corner_pos:
            verts.append(p); lni.append(ln)
        # glcmd strip emit order: c0, c1, c3, c2 with texcoords
        emit = [(0, 0.0, 0.0), (1, 1.0, 0.0), (3, 0.0, 1.0), (2, 1.0, 1.0)]
        glcmds.append(4)  # positive -> triangle strip of 4 verts
        idxs = []
        for (ci, s, t) in emit:
            glcmds.append(struct.unpack("<I", struct.pack("<f", s))[0])
            glcmds.append(struct.unpack("<I", struct.pack("<f", t))[0])
            glcmds.append(base + ci)
            idxs.append(base + ci)
        # two triangles of the strip (for the file's tri table only)
        tris.append((idxs[0], idxs[1], idxs[2]))
        tris.append((idxs[2], idxs[1], idxs[3]))
    glcmds.append(0)  # terminator

    num_xyz = len(verts)          # 24
    num_st = num_xyz              # 24 (required > 0; gl uses glcmds)
    num_tris = len(tris)          # 12
    num_frames = 1
    num_glcmds = len(glcmds)
    num_skins = 1

    # frame byte encoding: pos in [-H,+H] -> v = (pos - translate)/scale
    translate = [-H, -H, -H]
    scale = [(2 * H) / 255.0] * 3
    framesize = 40 + num_xyz * 4   # scale[3]+translate[3]+name[16] = 40

    # offsets
    ofs_skins = 68
    ofs_st = ofs_skins + num_skins * 64
    ofs_tris = ofs_st + num_st * 4
    ofs_frames = ofs_tris + num_tris * 12
    ofs_glcmds = ofs_frames + num_frames * framesize
    ofs_end = ofs_glcmds + num_glcmds * 4

    out = bytearray()
    ident = struct.unpack("<I", b"IDP2")[0]
    out += struct.pack("<iiiiii", ident, 8, skin_w, skin_h, framesize, num_skins)
    out += struct.pack("<iiiiii", num_xyz, num_st, num_tris, num_glcmds, num_frames, ofs_skins)
    out += struct.pack("<iiiii", ofs_st, ofs_tris, ofs_frames, ofs_glcmds, ofs_end)
    assert len(out) == ofs_skins, len(out)

    # skins
    sk = skin_name.encode("ascii")[:63]
    out += sk + b"\x00" * (64 - len(sk))

    # st verts (short s,t) - normalised texcoords scaled to skin size
    st_map = {}
    for f in range(6):
        emit = [(0, 0.0, 0.0), (1, 1.0, 0.0), (3, 0.0, 1.0), (2, 1.0, 1.0)]
        for (ci, s, t) in emit:
            st_map[f * 4 + ci] = (int(s * (skin_w - 1)), int(t * (skin_h - 1)))
    for i in range(num_st):
        s, t = st_map.get(i, (0, 0))
        out += struct.pack("<hh", s, t)

    # triangles
    for (a, b, c) in tris:
        out += struct.pack("<hhh", a, b, c)
        out += struct.pack("<hhh", a, b, c)  # index_st (unused by gl3)

    # frame 0
    out += struct.pack("<fff", *scale)
    out += struct.pack("<fff", *translate)
    name = b"frame0"
    out += name + b"\x00" * (16 - len(name))
    for i, p in enumerate(verts):
        b0 = int(round((p[0] - translate[0]) / scale[0]))
        b1 = int(round((p[1] - translate[1]) / scale[1]))
        b2 = int(round((p[2] - translate[2]) / scale[2]))
        b0 = max(0, min(255, b0)); b1 = max(0, min(255, b1)); b2 = max(0, min(255, b2))
        out += struct.pack("<BBBB", b0, b1, b2, lni[i] & 0xFF)

    # glcmds
    for g in glcmds:
        out += struct.pack("<I", g & 0xFFFFFFFF)

    assert len(out) == ofs_end, (len(out), ofs_end)
    with open(path, "wb") as f:
        f.write(bytes(out))
    return len(out), num_xyz, num_tris, num_glcmds, len(anorms)


def write_conchars(path, w=128, hgt=128):
    """128x128 8bpp PCX console font.  GL3_Draw_InitLocal() fatally errors if
    pics/conchars.pcx won't load, but this render loop never draws console text,
    so the glyph content is a placeholder 16x16 grid of boxed cells.  8bpp pics
    are colourised through d_8to24table (colormap.pcx), so no trailing palette
    is needed here; index 0 is background, non-zero draws the glyph boxes."""
    planes = 1
    bpl = w
    body = bytearray()
    for y in range(hgt):
        row = bytearray()
        for x in range(w):
            cx = x % 8; cy = y % 8
            # box outline for each 8x8 cell -> a visible placeholder glyph
            edge = (cx == 0 or cy == 0 or cx == 7 or cy == 7)
            row.append(200 if edge else 0)
        body += rle_encode(bytes(row))
    data = pcx_header(w, hgt, planes, 8, bpl) + bytes(body)
    with open(path, "wb") as f:
        f.write(data)
    return len(data)


def main():
    pics = os.path.join(RES, "pics")
    model = os.path.join(RES, "models", "probe")
    os.makedirs(pics, exist_ok=True)
    os.makedirs(model, exist_ok=True)

    n = write_colormap(os.path.join(pics, "colormap.pcx"))
    print("colormap.pcx: %d bytes" % n)

    n = write_conchars(os.path.join(pics, "conchars.pcx"))
    print("conchars.pcx: %d bytes" % n)

    n, sw, sh = write_skin(os.path.join(model, "skin.pcx"))
    print("skin.pcx: %d bytes (%dx%d RGBA)" % (n, sw, sh))

    n, nxyz, ntris, ngl, nan = write_md2(os.path.join(model, "tris.md2"))
    print("tris.md2: %d bytes (xyz=%d tris=%d glcmds=%d, anorms=%d)"
          % (n, nxyz, ntris, ngl, nan))


if __name__ == "__main__":
    main()
