#!/usr/bin/env python3
#
# Draws res/icon.png for the qucs-s port: the launcher looks for
# /apps/qucs-s/res/icon.png (system/xwin/bin/xlauncher/xlauncher.cc) and every
# other app in projects/qt/apps ships a 256x256 RGBA one.
#
# The picture is the program in one glyph - a sheet of the grid Qucs draws on,
# a wire with a resistor in it and the sine a simulation of it plots - in the
# two colours the schematic canvas itself uses: the dark blue of an element and
# the red of a traced curve.  Drawn at 4x and scaled down, so the strokes come
# out smooth at the size it is actually shown.

import math
import os

from PIL import Image, ImageDraw

S = 4                     # supersampling
N = 256 * S

INK = (0, 0, 130, 255)    # components.cpp paints the elements this
TRACE = (200, 0, 0, 255)  # and the selection, which is what a trace reads as
GRID = (219, 219, 219, 255)
EDGE = (190, 199, 212, 255)

img = Image.new("RGBA", (N, N), (0, 0, 0, 0))
d = ImageDraw.Draw(img)

# ---- the sheet --------------------------------------------------------------
# A rounded card with a faint gradient, the way a page of paper sits on a desk.
sheet = [24 * S, 24 * S, (256 - 24) * S, (256 - 24) * S]
for i in range(N):
    t = i / float(N - 1)
    shade = tuple(int(round(a + (b - a) * t)) for a, b in ((255, 236), (255, 240), (255, 248)))
    d.line([(sheet[0], i), (sheet[2], i)], fill=shade + (255,))
mask = Image.new("L", (N, N), 0)
ImageDraw.Draw(mask).rounded_rectangle(sheet, radius=44 * S, fill=255)
img.putalpha(mask)

d = ImageDraw.Draw(img)
d.rounded_rectangle(sheet, radius=44 * S, outline=EDGE, width=3 * S)

# ---- the grid ---------------------------------------------------------------
# kGrid is 10 units on the schematic; here it is the dots the canvas paints.
for gy in range(56, 256 - 40, 16):
    for gx in range(56, 256 - 40, 16):
        x, y = gx * S, gy * S
        d.ellipse([x - 2 * S, y - 2 * S, x + 2 * S, y + 2 * S], fill=GRID)


def poly(points, fill, width):
    d.line(points, fill=fill, width=width, joint="curve")
    for x, y in (points[0], points[-1]):
        d.ellipse([x - width / 2, y - width / 2, x + width / 2, y + width / 2], fill=fill)


W = 10 * S          # stroke
CY = 128 * S        # the wire's height

# ---- the resistor -----------------------------------------------------------
# The zigzag components.cpp draws for "R", leads included, sitting on the wire.
rx0, rx1 = 70 * S, 130 * S
zag = [(50 * S, CY), (rx0, CY)]
step = (rx1 - rx0) / 4.0
for k in range(4):
    zag.append((rx0 + step * (k + 0.5), CY + (24 * S if k % 2 == 0 else -24 * S)))
zag.append((rx1, CY))
zag.append((150 * S, CY))
poly(zag, INK, W)

# ---- the traced curve -------------------------------------------------------
# One and a half periods of a sine out of the resistor: what a transient run of
# that circuit puts in a diagram.
pts = []
x0, x1 = 150 * S, (256 - 50) * S
for i in range(0, 401):
    t = i / 400.0
    x = x0 + (x1 - x0) * t
    y = CY - 44 * S * math.sin(2.0 * math.pi * 1.5 * t)
    pts.append((x, y))
poly(pts, TRACE, W)

# ---- the terminals ----------------------------------------------------------
# Where the wire ends: a port, drawn the way the canvas draws a junction.
for x in (50 * S, (256 - 50) * S):
    r = 8 * S
    d.ellipse([x - r, CY - r, x + r, CY + r], fill=INK)

out = img.resize((256, 256), Image.LANCZOS)
here = os.path.dirname(os.path.abspath(__file__))
out.save(os.path.join(here, "res", "icon.png"))
print("wrote", os.path.join(here, "res", "icon.png"), out.size, out.mode)
