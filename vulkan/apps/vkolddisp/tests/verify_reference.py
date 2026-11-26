#!/usr/bin/env python3
"""Pixel-exact check of vkolddisp against live Shadertoy lc3BRn captures.

reference/ref_t1_25.png and ref_t1_75.png were captured from the built-in
browser (canvas.toDataURL) while driving gShaderToy.mEffect.Paint(iTime,...)
with iTime = 1.25 / 1.75 (the two blink phases).  Both captures landed inside
host second 1795729008 = 2026-11-27 05:36:48 local, i.e. iDate =
(2026, 10, 27, 20208); Shadertoy's iDate follows the browser's local clock.

Build the probe first (see host_render.c header), then run:
    python3 verify_reference.py [path-to-host_render]
Exit code 0 = every pixel matches in both phases.
"""
import os
import struct
import subprocess
import sys
import tempfile
import zlib

HERE = os.path.dirname(os.path.abspath(__file__))
W, H = 803, 452
DATE = ('2026', '10', '27', '20208')


def decode_png(path):
    d = open(path, 'rb').read()
    assert d[:8] == b'\x89PNG\r\n\x1a\n'
    pos, idat, w, h, ct = 8, b'', None, None, None
    while pos < len(d):
        ln = struct.unpack('>I', d[pos:pos + 4])[0]
        typ = d[pos + 4:pos + 8]
        data = d[pos + 8:pos + 8 + ln]
        if typ == b'IHDR':
            w, h, depth, ct, _, _, inter = struct.unpack('>IIBBBBB', data)
            assert depth == 8 and inter == 0 and ct in (2, 6)
        elif typ == b'IDAT':
            idat += data
        pos += 12 + ln
    raw = zlib.decompress(idat)
    nch = 4 if ct == 6 else 3
    stride = w * nch
    out = bytearray(w * h * 4)
    prev = bytearray(stride)
    p = 0
    for y in range(h):
        f = raw[p]
        p += 1
        line = bytearray(raw[p:p + stride])
        p += stride
        if f == 1:
            for i in range(nch, stride):
                line[i] = (line[i] + line[i - nch]) & 255
        elif f == 2:
            for i in range(stride):
                line[i] = (line[i] + prev[i]) & 255
        elif f == 3:
            for i in range(stride):
                a = line[i - nch] if i >= nch else 0
                line[i] = (line[i] + ((a + prev[i]) >> 1)) & 255
        elif f == 4:
            for i in range(stride):
                a = line[i - nch] if i >= nch else 0
                b = prev[i]
                c = prev[i - nch] if i >= nch else 0
                pa, pb, pc = abs(b - c), abs(a - c), abs(a + b - 2 * c)
                pr = a if (pa <= pb and pa <= pc) else (b if pb <= pc else c)
                line[i] = (line[i] + pr) & 255
        for x in range(w):
            o, i = (y * w + x) * 4, x * nch
            out[o:o + 3] = line[i:i + 3]
            out[o + 3] = line[i + 3] if nch == 4 else 255
        prev = line
    return w, h, bytes(out)


def main():
    probe = sys.argv[1] if len(sys.argv) > 1 else os.path.join(HERE, 'host_render')
    if not os.path.exists(probe):
        sys.exit('build host_render first (see host_render.c header)')
    bad = 0
    for t in ('1_25', '1_75'):
        ref = os.path.join(HERE, 'reference', 'ref_t%s.png' % t)
        rw, rh, rp = decode_png(ref)
        assert (rw, rh) == (W, H)
        with tempfile.NamedTemporaryFile(suffix='.rgba') as tmp:
            r = subprocess.run([probe, str(W), str(H), t.replace('_', '.'),
                                tmp.name] + list(DATE),
                               capture_output=True, text=True)
            sys.stdout.write(r.stdout)
            if r.returncode != 0:
                sys.exit('probe failed:\n' + r.stderr)
            op = open(tmp.name, 'rb').read()
        diff = sum(1 for i in range(W * H)
                   if rp[i * 4:i * 4 + 3] != op[i * 4:i * 4 + 3])
        print('iTime=%s: %d/%d pixels differ' % (t.replace('_', '.'), diff, W * H))
        bad += diff
    print('PIXEL-EXACT' if bad == 0 else 'MISMATCH')
    return 1 if bad else 0


if __name__ == '__main__':
    sys.exit(main())