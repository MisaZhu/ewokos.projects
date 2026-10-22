#!/usr/bin/env python3
"""Preserve the reference bytes and generate equivalent, scratch-free lookups."""
import argparse
import hashlib
import json
from pathlib import Path
import re
import struct
import zlib

APP = Path(__file__).resolve().parents[1]
HASHES = {
    'common': '617ce10d3599e86b266022394751216efeb8fd726444010548c4d23e6fa8e62e',
    'image': '88df4a5379ad9b2f3d0b4381a2cda60534a835dd8c8c3e8ad29b52fd87ce09de',
}
VS = 'attribute vec3 aPosition;\nvoid main() { gl_Position = vec4(aPosition, 1.0); }\n'
PREFIX = 'precision highp float;\nprecision highp int;\nuniform vec3 iResolution;\nuniform float iTime;\n'
SUFFIX = '\nvoid main() { vec4 color; mainImage(color, gl_FragCoord.xy); gl_FragColor = color; }\n'
ADAPTATION = ('GPU: original integer tables losslessly packed into an embedded 4x512 RGBA8 nearest-filtered data texture (LINEARTILE); '
              'fractional tables use exact constant lookups, eliminating V3D array scratch. '
              'CPU: original in-memory tables with explicit lengths and equivalent 2x2 inverse; '
              'reuse identical main/shadow cells and skip brush walks outside their conservative bounds. '
              'Original res/Common and Image bytes, scene math, artwork, camera, timing and resolution are preserved.')
_LOOKUP_HELPERS = ''


def write_changed(path, data):
    path.parent.mkdir(parents=True, exist_ok=True)
    if isinstance(data, str):
        data = data.encode()
    if not path.exists() or path.read_bytes() != data:
        path.write_bytes(data)


def import_reference(path):
    shader = json.loads(Path(path).read_text())
    assert shader['info']['id'] == 'fctSzf'
    passes = shader['renderpass']
    assert len(passes) == 2 and {p['type'] for p in passes} == set(HASHES)
    for p in passes:
        assert not p['inputs'], 'Unexpected channel input'
        source = p['code'].encode()
        assert hashlib.sha256(source).hexdigest() == HASHES[p['type']]
        write_changed(APP / ('res/' + p['type'] + '.glsl'), source)
    manifest = {
        'url': 'https://www.shadertoy.com/view/fctSzf',
        **shader['info'],
        'sha256': HASHES,
        'inputs': [],
        'assets': 'Original Geneva 9 glyphs, MacWriteEmpty and MacPaintHello bitmaps, cursors and brush path are embedded in Common.',
        'license': 'No explicit license in retrieved source or metadata; no additional rights asserted.',
        'adaptation': ADAPTATION,
    }
    write_changed(APP / 'res/source.json', json.dumps(manifest, ensure_ascii=False, indent=2) + '\n')


def lower_cpu(source):
    # The interpreter requires explicit array sizes and has no array.length().
    # Preserve every element and expression, including the original 32-bit masks.
    sizes = {}
    pattern = r'const (int|float|vec2|vec4) (\w+)\[\] = \1\[\]\((.*?)\);'
    def explicit(match):
        kind, name, body = match.groups()
        depth, count = 0, 1
        for ch in body:
            if ch == '(':
                depth += 1
            elif ch == ')':
                depth -= 1
            elif ch == ',' and depth == 0:
                count += 1
        assert depth == 0 and body.strip()
        sizes[name] = count
        return f'const {kind} {name}[{count}] = {kind}[{count}]({body});'
    source = re.sub(pattern, explicit, source, flags=re.S)
    assert sizes == {'FONT_ROWS': 252, 'ADVANCE': 21, 'LINE0': 4, 'TXT': 50,
                     'SCR1': 384, 'SCR2': 432, 'HELLOW': 10, 'ARROW': 16,
                     'IBEAM': 16, 'PTS': 138, 'PGCUM': 9, 'PGBB': 9, 'K': 8}, sizes
    for name, count in sizes.items():
        source = source.replace(name + '.length()', str(count))
    assert '.length()' not in source and '[]' not in source
    # BF_INVERSE in the interpreter returns scalar zero. Spell out the exact
    # adjugate/determinant operation here without changing the GPU reference.
    assert source.count('inverse(') == 1
    source = source.replace('inverse(', 'macglInverse(')
    inverse = '''mat2 macglInverse(mat2 m) {
  float a = m[0][0], b = m[0][1], c = m[1][0], d = m[1][1];
  return mat2(d, -b, -c, a) / (a*d - c*b);
}
'''
    return inverse + source


def lower_lookups(source):
    # V3D lowers dynamic constant arrays to scratch, initialized per invocation.
    # Store integer tables losslessly in one immutable RGBA8 texture instead.
    # Only the two tiny fractional tables use constant decision trees.
    arrays = {}
    pattern = r'const (int|float|vec2|vec4) (\w+)\[\d*\] = \1\[\d*\]\((.*?)\);'

    def collect(match):
        kind, name, body = match.groups()
        values, depth, start = [], 0, 0
        for i, ch in enumerate(body):
            if ch == '(':
                depth += 1
            elif ch == ')':
                depth -= 1
            elif ch == ',' and depth == 0:
                values.append(body[start:i].strip())
                start = i + 1
        values.append(body[start:].strip())
        assert depth == 0 and all(values)
        arrays[name] = (kind, values)
        return ''

    source = re.sub(pattern, collect, source, flags=re.S)
    assert len(arrays) == 13
    global _LOOKUP_HELPERS
    # The data texture is 4x512 so pglc_tex_layout_2d picks LINEARTILE (width
    # <= utile_w), not the UIF layout a 64x32 image lands in. The V3D compiled
    # sampler mis-fetches exact texels through the UIF-tiled ctex (its implicit
    # XOR permutes the x>=32 macroblock column), so the LINEARTILE layout is
    # used to keep every atlas byte addressable bit-exactly. Texel i is still at
    # byte i*4; only the row width (and thus the UV decode) changes.
    atlas = bytearray()
    helpers = ['''uniform sampler2D macglData;
vec4 macglBytes(int i) {
  vec2 uv = (vec2(float(i & 3), float(i >> 2)) + 0.5) / vec2(4, 512);
  return floor(texture2D(macglData, uv) * 255.0 + 0.5);
}
int macglWord(int i) {
  ivec4 b = ivec4(macglBytes(i));
  return b.x | (b.y << 8) | (b.z << 16) | (b.w << 24);
}
''']
    for name, (kind, values) in arrays.items():
        source = source.replace(name + '.length()', str(len(values)))
        def lookup(match):
            index = match.group(1).strip()
            if index.isdigit():
                return f'{kind}({values[int(index)]})'
            return f'macgl_{name}({index})'
        source = re.sub(r'\b' + name + r'\[([^\[\]]+)\]', lookup, source)
        assert not re.search(r'\b' + name + r'\[', source)

        def tree(lo, hi, indent):
            if all(v == values[lo] for v in values[lo:hi]):
                return indent + f'return {kind}({values[lo]});\n'
            mid = (lo + hi) // 2
            return (indent + f'if (i < {mid}) {{\n' + tree(lo, mid, indent + '  ') +
                    indent + '} else {\n' + tree(mid, hi, indent + '  ') + indent + '}\n')
        if name in ('PGCUM', 'K'):
            body = tree(0, len(values), '  ')
        else:
            offset = len(atlas) // 4
            for value in values:
                if kind == 'int':
                    atlas.extend(struct.pack('<I', int(value) & 0xffffffff))
                else:
                    coords = [int(v.strip()) for v in value[value.index('(')+1:-1].split(',')]
                    assert len(coords) == int(kind[-1]) and all(0 <= v <= 255 for v in coords)
                    atlas.extend(coords + [0] * (4 - len(coords)))
            expr = f'macglWord(i + {offset})' if kind == 'int' else f'macglBytes(i + {offset})'
            if kind == 'vec2':
                expr += '.xy'
            body = f'  return {expr};\n'
        helpers.append(f'{kind} macgl_{name}(int i) {{\n' + body + '}\n')
    assert len(atlas) <= 4 * 512 * 4
    atlas.extend(bytes(4 * 512 * 4 - len(atlas)))
    _LOOKUP_HELPERS = helpers[0]
    return '\n'.join(helpers) + source, bytes(atlas)


def optimize_cpu_scene(source):
    # Every stroke/nib/period lies within this inclusive cell rectangle, even
    # during the toolbox glide and final exit. Outside it, walking the last
    # path group just to locate the pen cannot affect the returned screen bit.
    # Keep the independent arrow cursor and all static UI outside this gate.
    old = 'if (drawU > 0.){'
    new = 'if (drawU > 0. && p.x >= 36 && p.x <= 256 && p.y >= 53 && p.y <= 129){'
    assert source.count(old) == 1
    source = source.replace(old, new)
    old = 'if (t > 4.3 && t < 25.1){'
    assert source.count(old) == 1
    source = source.replace(old, 'if (t > 4.3 && t < 25.1 && p.x >= 36 && p.x <= 256 && p.y >= 53 && p.y <= 129){')
    # Both screens and both cursor overlays depend only on floor(c), never on
    # its fractional part. Reuse the completed main bit when the shadow is in
    # that same cell; coverage/lighting still run at the original coordinates.
    old = 'int fgSh = scene == 0 ? screen1(csh, ts) : screen2(csh, t2);'
    new = '''int fgSh = fgMain;
    if (floor(c.x) != floor(csh.x) || floor(c.y) != floor(csh.y)) {
    fgSh = scene == 0 ? screen1(csh, ts) : screen2(csh, t2);'''
    assert source.count(old) == 1
    source = source.replace(old, new)
    old = '    if (fgSh == 1){'
    assert source.count(old) == 1
    return source.replace(old, '    }\n' + old)


def probe_shader():
    # On-device texel probe: lay every atlas byte out as grayscale so a single
    # framebuffer dump reveals exactly how the V3D compiled sampler returns each
    # stored byte (permutation / scaling / addressing). Screen (x,y) with
    # x in 0..255, y in 0..31 maps to texel t = y*64 + x/4, channel c = x%4.
    return (PREFIX + _LOOKUP_HELPERS +
            'void main() {\n'
            '  vec2 fc = gl_FragCoord.xy;\n'
            '  int cx = int(floor(fc.x));\n'
            '  int cy = int(floor(fc.y));\n'
            '  int t = (cy << 6) | (cx >> 2);\n'
            '  int c = cx & 3;\n'
            '  vec4 b = macglBytes(t);\n'
            '  float v = b.x;\n'
            '  if (c == 1) v = b.y;\n'
            '  else if (c == 2) v = b.z;\n'
            '  else if (c == 3) v = b.w;\n'
            '  gl_FragColor = vec4(v / 255.0, v / 255.0, v / 255.0, 1.0);\n'
            '}\n')


def generate():
    bodies = []
    for name, digest in HASHES.items():
        body = (APP / ('res/' + name + '.glsl')).read_bytes()
        assert hashlib.sha256(body).hexdigest() == digest, name + ' reference changed'
        bodies.append(body.decode())
    source = '\n'.join(bodies)
    manifest = json.loads((APP / 'res/source.json').read_text())
    manifest['adaptation'] = ADAPTATION
    write_changed(APP / 'res/source.json', json.dumps(manifest, ensure_ascii=False, indent=2) + '\n')
    optimized = optimize_cpu_scene(source)
    # Measured V3D compilation: lookup-only lowering eliminates scratch entirely.
    # The CPU branch optimizations add GPU register spills, so leave them CPU-only.
    gpu_body, atlas = lower_lookups(source)
    frontend_body, frontend_atlas = lower_lookups(lower_cpu(source))
    assert atlas == frontend_atlas
    gpu = PREFIX + gpu_body + SUFFIX
    frontend = PREFIX + frontend_body + SUFFIX
    # The CPU interpreter has efficient arrays but no hardware texture cache;
    # keep its tables in memory and eliminate redundant screen/path evaluations.
    cpu = PREFIX + lower_cpu(optimized) + SUFFIX
    write_changed(APP / 'gen/macgl_data.rgba', atlas)
    write_changed(APP / 'gen/macgl_gpu_cpu.frag', frontend)
    write_changed(APP / 'gen/macgl_reference.frag', PREFIX + source + SUFFIX)
    write_changed(APP / 'gen/macgl_cpu_reference.frag', PREFIX + lower_cpu(source) + SUFFIX)
    write_changed(APP / 'gen/macgl.frag', gpu)
    write_changed(APP / 'gen/macgl_cpu.frag', cpu)
    header = '/* Generated by tools/shader.py; original sources and provenance in res/. */\n#ifndef MACGL_GLSL_H\n#define MACGL_GLSL_H\n'
    for name, value in [('macgl_vs_glsl', VS), ('macgl_fs_glsl', frontend),
                        ('macgl_cpu_fs_glsl', cpu), ('macgl_gpu_fs_glsl', gpu),
                        ('macgl_probe_fs_glsl', probe_shader()),
                        ('macgl_reference_gpu_fs_glsl', PREFIX + source + SUFFIX)]: 
        header += 'static const char *const ' + name + ' =\n'
        header += '\n'.join(json.dumps(line + '\n') for line in value.splitlines()) + ';\n'
    header += 'static const unsigned char macgl_data[64 * 32 * 4] = {\n'
    header += '\n'.join(','.join(str(b) for b in atlas[i:i+32]) + ',' for i in range(0, len(atlas), 32))
    header += '\n};\nstatic const char *const macgl_reference_fs_glsl =\n'
    header += '\n'.join(json.dumps(line + '\n') for line in (PREFIX + lower_cpu(source) + SUFFIX).splitlines())
    header += ';\n'
    write_changed(APP / 'gen/macgl_glsl.h', header + '#endif\n')


def import_frames(directory):
    directory = Path(directory)
    samples, frames = bytearray(), []
    times = [0, 1, 3, 7, 10, 16, 18, 20, 24, 26, 27, 30, 31, 32, 40, 49, 50, 54, 56, 57]
    for time in times:
        rgba = (directory / f'ref-{time}.rgba').read_bytes()
        assert len(rgba) == 320 * 180 * 4
        frames.append({'time': time, 'sha256': hashlib.sha256(rgba).hexdigest()})
        for y in range(5, 180, 17):
            for x in range(7, 320, 19):
                offset = (y * 320 + x) * 4
                assert rgba[offset + 3] == 255
                samples.extend(rgba[offset:offset + 3])
    assert len(samples) == 20 * 187 * 3
    write_changed(APP / 'tests/reference.rgb', bytes(samples))
    metadata = {'source_sha256': HASHES, 'resolution': [320, 180],
                'origin': 'bottom-left', 'sample_x': [7, 320, 19], 'sample_y': [5, 180, 17],
                'renderer': 'Original Common + Image in WebGL2, highp, RGBA8, no dithering',
                'frames': frames, 'sample_sha256': hashlib.sha256(samples).hexdigest()}
    write_changed(APP / 'tests/reference.json', json.dumps(metadata, indent=2) + '\n')


def import_icon(path):
    # Launcher artwork is a crop from a rendered original frame, never a shader input.
    rgba = Path(path).read_bytes()
    assert len(rgba) == 320 * 180 * 4
    rows = [b'\0' + rgba[((179-y)*320+112)*4:((179-y)*320+208)*4] for y in range(42, 138)]
    def chunk(kind, data):
        return struct.pack('>I', len(data)) + kind + data + struct.pack('>I', zlib.crc32(kind + data))
    png = b'\x89PNG\r\n\x1a\n' + chunk(b'IHDR', struct.pack('>2I5B', 96, 96, 8, 6, 0, 0, 0))
    png += chunk(b'IDAT', zlib.compress(b''.join(rows))) + chunk(b'IEND', b'')
    write_changed(APP / 'res/icon.png', png)


if __name__ == '__main__':
    parser = argparse.ArgumentParser()
    parser.add_argument('--import-reference')
    parser.add_argument('--icon-from-rgba')
    parser.add_argument('--import-frames')
    args = parser.parse_args()
    if args.import_reference:
        import_reference(args.import_reference)
    if args.icon_from_rgba:
        import_icon(args.icon_from_rgba)
    if args.import_frames:
        import_frames(args.import_frames)
    generate()
