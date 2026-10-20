#!/usr/bin/env python3
"""Generate a VALID Quake2 md2 probe model (textured cube) + 8-bit PCX skin.

The previous res/models/probe/tris.md2 had an empty/garbage glcmds order list,
so the md2 strip/fan walker produced almost no drawable geometry (the 'dots').
md2 geometry is driven by the glcmds ORDER list (strips/fans), so this writer
emits one 4-vertex triangle strip per cube face plus a coherent frame, triangle
list and st table.
"""
import struct, math, os

OUT = os.path.join(os.path.dirname(__file__), '..', 'res', 'models', 'probe')
OUT = os.path.abspath(OUT)

# ---- cube: 6 faces x 4 verts (strip order) ----
# face vertex positions (unit cube centred at origin, scaled to 32 units)
S = 32.0
faces = [
    # +X
    [(1,-1,-1),(1,1,-1),(1,-1,1),(1,1,1)],
    # -X
    [(-1,1,-1),(-1,-1,-1),(-1,1,1),(-1,-1,1)],
    # +Y
    [(-1,1,-1),(1,1,-1),(-1,1,1),(1,1,1)],
    # -Y
    [(1,-1,-1),(-1,-1,-1),(1,-1,1),(-1,-1,1)],
    # +Z
    [(-1,-1,1),(1,-1,1),(-1,1,1),(1,1,1)],
    # -Z
    [(1,-1,-1),(-1,-1,-1),(1,1,-1),(-1,1,-1)],
]
# per-face UV quad (normalized into a 3x2 atlas grid so each face gets a tile)
tiles = [(0,0),(1,0),(2,0),(0,1),(1,1),(2,1)]
uvs = [
    [(0,0),(1,0),(0,1),(1,1)],
    [(1,0),(0,0),(1,1),(0,1)],
    [(0,0),(1,0),(0,1),(1,1)],
    [(1,0),(0,0),(1,1),(0,1)],
    [(0,0),(1,0),(0,1),(1,1)],
    [(1,0),(0,0),(1,1),(0,1)],
]

verts = []   # (x,y,z)
stuvs = []   # (u,v) normalized
lnorms = []  # lightnormalindex per vertex (index into r_avertexnormals/anorms.h)
order = []   # glcmds ints
tris = []    # (i0,i1,i2, s0,s1,s2)

# anorms.h axis-aligned normal indices (0-based) for +X,-X,+Y,-Y,+Z,-Z, matching
# the `faces` order below.  The per-vertex light is shadedots[lightnormalindex],
# so a correct index keeps each cube face lit as the model spins.
FACE_LNORM = [52, 143, 32, 104, 5, 84]

for f, face in enumerate(faces):
    base = len(verts)
    for k in range(4):
        verts.append((face[k][0]*S, face[k][1]*S, face[k][2]*S))
        tu, tv = uvs[f][k]
        cu, cv = tiles[f]
        stuvs.append(((cu + tu) / 3.0, (cv + tv) / 2.0))
        lnorms.append(FACE_LNORM[f])
    # strip of 4 -> 2 triangles
    order.append(4)
    for k in range(4):
        order.append(struct.unpack('<I', struct.pack('<f', stuvs[base+k][0]))[0])
        order.append(struct.unpack('<I', struct.pack('<f', stuvs[base+k][1]))[0])
        order.append(base + k)
    tris.append((base+0, base+1, base+2, base+0, base+1, base+2))
    tris.append((base+1, base+3, base+2, base+1, base+3, base+2))
order.append(0)

num_xyz = len(verts)
num_st = len(stuvs)
num_tris = len(tris)
num_frames = 1
frame_size = 40 + num_xyz * 4
num_glcmds = len(order)

# ---- frame 0: scale/translate + quantized verts ----
# md2 vertex bytes are UNSIGNED (dtrivertx_t.v is `byte`), decoded by the
# refresher as  real = v * scale + translate  (see LerpVerts in gl3_mesh.c),
# and CullAliasModel rebuilds the bbox as  mins = translate,
# maxs = translate + scale*255.  So the [-S,+S] cube must be quantized into
# [0,255] with translate = -S (the mins corner) and scale = 2S/255.  Quantizing
# with SIGNED bytes around translate=0 (the old bug) makes -S and +S decode to
# nearly the same value (~+S), collapsing the whole cube into a sub-pixel blob.
translate = (-S, -S, -S)
scale = (2.0*S/255.0, 2.0*S/255.0, 2.0*S/255.0)
frame_verts = b''
for i, (x, y, z) in enumerate(verts):
    qx = int(round((x - translate[0]) / scale[0]))
    qy = int(round((y - translate[1]) / scale[1]))
    qz = int(round((z - translate[2]) / scale[2]))
    # clamp to unsigned byte
    qx = max(0, min(255, qx)); qy = max(0, min(255, qy)); qz = max(0, min(255, qz))
    frame_verts += struct.pack('<4B', qx, qy, qz, lnorms[i])

frame = struct.pack('<3f3f', *scale, *translate) + b'frame0'.ljust(16, b'\0') + frame_verts
assert len(frame) == frame_size, (len(frame), frame_size)

# ---- layout (canonical dmdl_t: 68-byte header, num_glcmds before num_frames) ----
ofs_skins = 68
ofs_st = ofs_skins + 64 * 1
ofs_tris = ofs_st + num_st * 4
ofs_frames = ofs_tris + num_tris * 12
ofs_glcmds = ofs_frames + num_frames * frame_size
ofs_end = ofs_glcmds + num_glcmds * 4

header = struct.pack('<4s16I',
                     b'IDP2', 8,
                     64, 64, frame_size, 1, num_xyz,
                     num_st, num_tris, num_glcmds, num_frames,
                     ofs_skins, ofs_st, ofs_tris, ofs_frames, ofs_glcmds, ofs_end)

skins = b'skin.pcx'.ljust(64, b'\0')
st = b''.join(struct.pack('<2H', int(round(u*63)), int(round(v*63))) for (u, v) in stuvs)
trib = b''.join(struct.pack('<6H', *t) for t in tris)
glcmds = struct.pack('<%dI' % num_glcmds, *order)

md2 = header + skins + st + trib + frame + glcmds
open(os.path.join(OUT, 'tris.md2'), 'wb').write(md2)
print('wrote tris.md2', len(md2), 'bytes  verts=%d tris=%d glcmds=%d' % (num_xyz, num_tris, num_glcmds))

# ---- 8-bit PCX skin: 3x2 atlas of coloured tiles with a border ----
W = H = 64
pal = []
colors = [(200,60,60),(60,200,60),(60,60,200),(200,200,60),(200,60,200),(60,200,200)]
for c in colors: pal += list(c)
pal += [0,0,0]* (256-6)
img = [[0]*W for _ in range(H)]
for y in range(H):
    for x in range(W):
        tx = min(2, x*3//W); ty = min(1, y*2//H)
        idx = ty*3+tx
        lx = x*3//W*0 + (x % (W//3)); ly = (y % (H//2))
        border = (lx < 2 or ly < 2 or lx >= W//3-2 or ly >= H//2-2)
        img[y][x] = 6 if border else idx  # 6 = black border index
# encode RLE
def rle(row):
    out = bytearray(); i = 0
    while i < len(row):
        v = row[i]; run = 1
        while i+run < len(row) and row[i+run] == v and run < 63: run += 1
        if run > 1 or v >= 0xC0:
            out += bytes([0xC0|run, v]); i += run
        else:
            out += bytes([v]); i += 1
    return bytes(out)
body = b''.join(rle(row) for row in img)
bpp_line = W
pcx = struct.pack('<BBBBHHHHHH48sBBHHHH54s',
                  0x0A, 5, 1, 8,
                  0, 0, W-1, H-1, 300, 300,
                  bytes(pal[:48]), 0, 1, bpp_line, 1, 0, 0, b'\0'*54)
pcx += body + b'\x0c' + bytes(pal)
open(os.path.join(OUT, 'skin.pcx'), 'wb').write(pcx)
print('wrote skin.pcx', len(pcx), 'bytes')
