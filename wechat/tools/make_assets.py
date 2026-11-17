#!/usr/bin/env python3
"""Asset pipeline for the wechat demo app (no third-party deps).

- downscales the generated demo photos into res/photo_*.png
- draws avatar_*.png and res/icon.png from graph primitives

Run from anywhere: paths are resolved relative to this file.
"""
import os
import struct
import zlib

HERE = os.path.dirname(os.path.abspath(__file__))
RES = os.path.join(HERE, "..", "res")
VIBE = os.path.expanduser("~/.qoder/vibe_images")


def png_read(path):
    with open(path, "rb") as f:
        data = f.read()
    assert data[:8] == b"\x89PNG\r\n\x1a\n", "not a png"
    pos = 8
    w = h = bitdepth = ctype = None
    idat = b""
    while pos < len(data):
        ln = struct.unpack(">I", data[pos:pos + 4])[0]
        typ = data[pos + 4:pos + 8]
        chunk = data[pos + 8:pos + 8 + ln]
        if typ == b"IHDR":
            w, h, bitdepth, ctype, comp, filt, inter = struct.unpack(">IIBBBBB", chunk)
            assert bitdepth == 8 and inter == 0, "unsupported png variant"
            assert ctype in (2, 6), "unsupported color type"
        elif typ == b"IDAT":
            idat += chunk
        elif typ == b"IEND":
            break
        pos += 12 + ln
    raw = zlib.decompress(idat)
    ch = 4 if ctype == 6 else 3
    stride = w * ch
    out = bytearray(w * h * 4)
    prev = bytearray(stride)
    p = 0
    for y in range(h):
        ft = raw[p]
        p += 1
        line = bytearray(raw[p:p + stride])
        p += stride
        if ft == 1:
            for i in range(ch, stride):
                line[i] = (line[i] + line[i - ch]) & 0xFF
        elif ft == 2:
            for i in range(stride):
                line[i] = (line[i] + prev[i]) & 0xFF
        elif ft == 3:
            for i in range(stride):
                a = line[i - ch] if i >= ch else 0
                line[i] = (line[i] + ((a + prev[i]) >> 1)) & 0xFF
        elif ft == 4:
            for i in range(stride):
                a = line[i - ch] if i >= ch else 0
                b = prev[i]
                c = prev[i - ch] if i >= ch else 0
                pa, pb, pc = abs(b - c), abs(a - c), abs(a + b - 2 * c)
                pr = a if (pa <= pb and pa <= pc) else (b if pb <= pc else c)
                line[i] = (line[i] + pr) & 0xFF
        for x in range(w):
            o = (y * w + x) * 4
            i = x * ch
            out[o] = line[i]
            out[o + 1] = line[i + 1]
            out[o + 2] = line[i + 2]
            out[o + 3] = line[i + 3] if ch == 4 else 255
        prev = line
    return w, h, out


def png_write(path, w, h, px):
    raw = b"".join(b"\x00" + bytes(px[y * w * 4:(y + 1) * w * 4]) for y in range(h))

    def chunk(typ, data):
        c = struct.pack(">I", len(data)) + typ + data
        return c + struct.pack(">I", zlib.crc32(typ + data) & 0xFFFFFFFF)

    ihdr = struct.pack(">IIBBBBB", w, h, 8, 6, 0, 0, 0)
    with open(path, "wb") as f:
        f.write(b"\x89PNG\r\n\x1a\n")
        f.write(chunk(b"IHDR", ihdr))
        f.write(chunk(b"IDAT", zlib.compress(raw, 9)))
        f.write(chunk(b"IEND", b""))


def downscale(w, h, px, max_side):
    if max(w, h) <= max_side:
        return w, h, px
    scale = max_side / float(max(w, h))
    nw, nh = max(1, int(w * scale)), max(1, int(h * scale))
    out = bytearray(nw * nh * 4)
    for y in range(nh):
        sy0, sy1 = int(y / scale), max(int(y / scale) + 1, int((y + 1) / scale))
        for x in range(nw):
            sx0, sx1 = int(x / scale), max(int(x / scale) + 1, int((x + 1) / scale))
            r = g = b = a = n = 0
            for sy in range(sy0, min(sy1, h)):
                for sx in range(sx0, min(sx1, w)):
                    o = (sy * w + sx) * 4
                    r += px[o]; g += px[o + 1]; b += px[o + 2]; a += px[o + 3]
                    n += 1
            o = (y * nw + x) * 4
            out[o] = r // n; out[o + 1] = g // n; out[o + 2] = b // n; out[o + 3] = a // n
    return nw, nh, out


class Canvas:
    def __init__(self, size):
        self.w = self.h = size
        self.px = bytearray(size * size * 4)

    def set(self, x, y, c):
        if 0 <= x < self.w and 0 <= y < self.h:
            o = (y * self.w + x) * 4
            self.px[o:o + 4] = bytes(c)

    def rect(self, x0, y0, x1, y1, c):
        for y in range(y0, y1):
            for x in range(x0, x1):
                self.set(x, y, c)

    def round_rect(self, x0, y0, x1, y1, r, c):
        for y in range(y0, y1):
            for x in range(x0, x1):
                dx = dy = 0
                if x < x0 + r: dx = x0 + r - x
                elif x >= x1 - r: dx = x - (x1 - r - 1)
                if y < y0 + r: dy = y0 + r - y
                elif y >= y1 - r: dy = y - (y1 - r - 1)
                if dx * dx + dy * dy <= r * r:
                    self.set(x, y, c)

    def circle(self, cx, cy, r, c):
        for y in range(cy - r, cy + r + 1):
            for x in range(cx - r, cx + r + 1):
                if (x - cx) ** 2 + (y - cy) ** 2 <= r * r:
                    self.set(x, y, c)

    def triangle(self, p0, p1, p2, c):
        minx = max(0, min(p0[0], p1[0], p2[0]))
        maxx = min(self.w - 1, max(p0[0], p1[0], p2[0]))
        miny = max(0, min(p0[1], p1[1], p2[1]))
        maxy = min(self.h - 1, max(p0[1], p1[1], p2[1]))

        def sign(a, b, p):
            return (a[0] - p[0]) * (b[1] - p[1]) - (b[0] - p[0]) * (a[1] - p[1])

        for y in range(miny, maxy + 1):
            for x in range(minx, maxx + 1):
                p = (x, y)
                d1, d2, d3 = sign(p0, p1, p), sign(p1, p2, p), sign(p2, p0, p)
                neg = (d1 < 0) or (d2 < 0) or (d3 < 0)
                pos = (d1 > 0) or (d2 > 0) or (d3 > 0)
                if not (neg and pos):
                    self.set(x, y, c)

    def save(self, name):
        png_write(os.path.join(RES, name), self.w, self.h, self.px)


def hexc(s, a=255):
    s = s.lstrip("#")
    return (int(s[0:2], 16), int(s[2:4], 16), int(s[4:6], 16), a)


WHITE = (255, 255, 255, 255)


def avatar(name, color, motif):
    cv = Canvas(96)
    bg = hexc(color)
    cv.round_rect(0, 0, 96, 96, 14, bg)
    motif(cv, bg)
    cv.save(name)


def m_bubble(cv, bg):
    cv.round_rect(20, 26, 76, 62, 12, WHITE)
    cv.triangle((32, 60), (44, 60), (30, 74), WHITE)
    for i, x in enumerate((34, 48, 62)):
        cv.circle(x, 44, 4, bg)


def m_mountain(cv, bg):
    cv.triangle((14, 72), (44, 28), (74, 72), WHITE)
    cv.triangle((48, 72), (68, 44), (88, 72), WHITE)
    cv.circle(70, 26, 8, WHITE)


def m_moon(cv, bg):
    cv.circle(48, 48, 24, WHITE)
    cv.circle(58, 42, 22, bg)


def m_diamond(cv, bg):
    cv.triangle((48, 20), (76, 48), (48, 76), WHITE)
    cv.triangle((48, 20), (20, 48), (48, 76), WHITE)


def m_house(cv, bg):
    cv.triangle((16, 46), (48, 20), (80, 46), WHITE)
    cv.rect(26, 46, 70, 76, WHITE)
    cv.rect(42, 56, 54, 76, bg)


def m_doc(cv, bg):
    cv.round_rect(28, 18, 68, 78, 4, WHITE)
    for y in (32, 44, 56):
        cv.rect(36, y, 60, y + 4, bg)


def make_icon():
    cv = Canvas(128)
    cv.round_rect(0, 0, 128, 128, 28, hexc("#07C160"))
    cv.round_rect(22, 30, 86, 76, 16, WHITE)
    cv.triangle((36, 72), (52, 72), (34, 90), WHITE)
    cv.round_rect(62, 56, 108, 92, 14, WHITE)
    cv.triangle((92, 88), (104, 88), (106, 102), WHITE)
    g = hexc("#07C160")
    for x in (40, 56, 72):
        cv.circle(x, 53, 4, g)
    for x in (76, 92):
        cv.circle(x, 74, 4, g)
    cv.save("icon.png")


def main():
    os.makedirs(RES, exist_ok=True)
    photos = [
        ("photo_mountain", "photo_mountain.png"),
        ("photo_city", "photo_city.png"),
    ]
    for prefix, out in photos:
        src = None
        for f in sorted(os.listdir(VIBE)):
            if f.startswith(prefix + "_") and f.endswith(".png"):
                src = os.path.join(VIBE, f)
        if src is None:
            print("skip (missing):", prefix)
            continue
        w, h, px = png_read(src)
        w, h, px = downscale(w, h, px, 512)
        png_write(os.path.join(RES, out), w, h, px)
        print("wrote", out, w, h)

    avatar("avatar_me.png", "#07C160", m_bubble)
    avatar("avatar_zhangsan.png", "#4A90D9", m_mountain)
    avatar("avatar_lisi.png", "#E6A23C", m_moon)
    avatar("avatar_wangwu.png", "#9B59B6", m_diamond)
    avatar("avatar_family.png", "#E74C3C", m_house)
    avatar("avatar_file.png", "#2C7BE5", m_doc)
    make_icon()
    print("avatars + icon done")


if __name__ == "__main__":
    main()