#!/usr/bin/env python3
"""Generate olddisp_font.h from the VERBATIM res/common.glsl font defines.
Only active (non-comment) #define ch_* lines are read; the last active
definition of a name wins (ch_0 ships commented-out alternatives).
ASCII 96 (backtick) has no glyph in the reference; its slot stays zero.
"""
import os
import re

HERE = os.path.dirname(os.path.abspath(__file__))
APP = os.path.join(HERE, '..')

NAMES = (['spc', 'exc', 'quo', 'hsh', 'dol', 'pct', 'amp', 'apo', 'lbr', 'rbr',
          'ast', 'crs', 'com', 'dsh', 'per', 'lsl'] +
         [str(d) for d in range(10)] +
         ['col', 'scl', 'les', 'equ', 'grt', 'que', 'ats'] +
         [chr(c) for c in range(ord('A'), ord('Z') + 1)] +
         ['lsb', 'rsl', 'rsb', 'pow', 'usc', 'grv'] +
         [chr(c) for c in range(ord('a'), ord('z') + 1)] +
         ['lpa', 'bar', 'rpa', 'tid', 'lar'])
assert len(NAMES) == 96

src = open(os.path.join(APP, 'res', 'common.glsl')).read()
glyphs = {}
for line in src.splitlines():
    if line.lstrip().startswith('//'):
        continue
    m = re.search(r'#define\s+ch_(\w+)\s+ivec4\(\s*(0x[0-9A-Fa-f]+)\s*,\s*(0x[0-9A-Fa-f]+)\s*,\s*(0x[0-9A-Fa-f]+)\s*,\s*(0x[0-9A-Fa-f]+)\s*\)', line)
    if m:
        glyphs[m.group(1)] = [int(m.group(i), 16) for i in range(2, 6)]

missing = [n for n in NAMES if n not in glyphs and n != 'grv']
if missing:
    raise SystemExit('missing glyphs: %s' % missing)

out = ['/*',
       ' * olddisp_font.h - the 96-bit 8x12 font table, GENERATED from res/common.glsl',
       ' * (Shadertoy lc3BRn Common tab, JoostAB / Flyguy) by tools/gen_font.py.',
       ' * Do not edit by hand.  Index = ASCII - 32; four 24-bit words pack the',
       ' * 8x12 glyph exactly like the reference ivec4 defines.  ASCII 96 has no',
       ' * glyph in the reference and stays zero.',
       ' */',
       '#ifndef VKOLDDISP_FONT_H',
       '#define VKOLDDISP_FONT_H',
       '',
       '#include <stdint.h>',
       '',
       'static const uint32_t olddisp_font[96][4] = {']
for i, n in enumerate(NAMES):
    v = glyphs.get(n, [0, 0, 0, 0])
    out.append('    { 0x%06X, 0x%06X, 0x%06X, 0x%06X }, /* %3d %s */' %
               (v[0], v[1], v[2], v[3], i + 32, n))
out += ['};', '', '#endif', '']
open(os.path.join(APP, 'olddisp_font.h'), 'w').write('\n'.join(out))
print('olddisp_font.h: %d glyphs (%d active defines)' % (len(NAMES), len(glyphs)))