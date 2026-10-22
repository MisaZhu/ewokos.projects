#!/usr/bin/env python3
"""Import the exact reference and generate the two existing GLES shader routes."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import shlex
import struct
import subprocess
import zlib

APP = Path(__file__).resolve().parents[1]
SOURCE_SHA256 = '7edb89ad32264834fdb6a955942902d71f9b33d08c9b4b908af8edc5fe4725e4'
VS = 'attribute vec3 aPosition;\nvoid main() { gl_Position = vec4(aPosition, 1.0); }\n'
PREFIX = 'precision highp float;\nprecision highp int;\nuniform vec3 iResolution;\nuniform float iTime;\nuniform sampler2D iChannel0;\n'
SUFFIX = '\nvoid main() { vec4 color; mainImage(color, gl_FragCoord.xy); gl_FragColor = vec4(color.rgb, 1.0); }\n'


def write_changed(path, data):
    path.parent.mkdir(parents=True, exist_ok=True)
    if isinstance(data, str):
        data = data.encode()
    if not path.exists() or path.read_bytes() != data:
        path.write_bytes(data)


def import_reference(path):
    raw = json.loads(Path(path).read_text())
    shader = raw[0]
    assert shader['info']['id'] == 'cs2GWD'
    assert len(shader['renderpass']) == 1
    image = shader['renderpass'][0]
    assert image['type'] == 'image'
    source = image['code'].encode()
    assert hashlib.sha256(source).hexdigest() == SOURCE_SHA256
    write_changed(APP / 'res/image.glsl', source)
    manifest = {
        'url': 'https://www.shadertoy.com/view/cs2GWD',
        'title': shader['info']['name'], 'author': shader['info']['username'],
        'description': shader['info']['description'],
        'source_sha256': SOURCE_SHA256,
        'license': 'No explicit license in the retrieved shader or metadata; no additional rights asserted.',
        'original_inputs': image['inputs'],
        'audio_mode': 'User-approved match of the current silent webpage: iChannel0 red is zero.',
        'runtime_sampler': '512x2 zero RGBA8, linear, repeat. Repeat is required by the compiled 2D texture route and is identical to clamp for this constant-zero input.',
        'visual_assets': 'Procedural geometry and materials in the unmodified Image source; no image textures.',
        'adaptation': 'GPU: distance-only marching, per-grid displacement reuse, looped six-sample normals, zero-contribution light rejection, and conservative upward-shadow escape above the animated panel height bound. Native resolution, ray budgets, normal samples, and materials preserved. Original shader retained for A/B validation and CPU rendering. CPU syntax expands macros and disambiguates sdbox overloads. Output alpha forced to one, as in Shadertoy.',
    }
    write_changed(APP / 'res/source.json', json.dumps(manifest, ensure_ascii=False, indent=2) + '\n')


def import_icon(path):
    # A crop from the captured original, not a substitute shader material.
    rgba = Path(path).read_bytes()
    assert len(rgba) == 320 * 180 * 4
    rows = []
    for y in range(96):
        sy = 179 - (42 + y)
        rows.append(b'\0' + rgba[(sy * 320 + 112) * 4:(sy * 320 + 208) * 4])
    def chunk(kind, data):
        return struct.pack('>I', len(data)) + kind + data + struct.pack('>I', zlib.crc32(kind + data))
    png = b'\x89PNG\r\n\x1a\n'
    png += chunk(b'IHDR', struct.pack('>2I5B', 96, 96, 8, 6, 0, 0, 0))
    png += chunk(b'IDAT', zlib.compress(b''.join(rows))) + chunk(b'IEND', b'')
    write_changed(APP / 'res/icon.png', png)


def optimize(source):
    # Preserve arithmetic and ray budgets. Material channels are needed only
    # at the final primary sample, not at every distance step or shadow step.
    # Keep primary material channels live nonetheless: deferring their lookup
    # changes GPU rounding and sparse pixels in native-resolution A/B captures.
    source = source.replace('float d;\n};', 'float d;\n  float zoffset;\n};', 1)
    offset = '.4*sin(2.*iTime+1.*fract(grid.h.z*28.)+.3*(grid.c.x+grid.c.y))'
    source = source.replace('  r.d=b;', '  r.d=b;\n  r.zoffset=' + offset.replace('grid.', 'r.') + ';')
    source = source.replace(offset, 'grid.zoffset')
    normal = '''vec3 nmap(vec3 p,Grid grid,float dd){
  vec3 n=vec3(0);
  float positive=0.;
  for(int i=0;i<6;i++){
    int axis=i/2;
    vec3 delta=vec3(0);
    delta[axis]=dd;
    bool plus=(i-2*axis)==0;
    float sampleDistance=map(plus?p+delta:p-delta,grid).x;
    if(plus) positive=sampleDistance;
    else n[axis]=positive-sampleDistance;
  }
  return normalize(n);
}
'''
    source = re.sub(r'vec3 nmap\([^}]+\}', normal, source)
    source = source.replace('int iter){', 'int iter,bool material){')
    # Both shadow directions point upward. Every panel stays below c.z+0.801
    # (including displacement), and neither 50-unit shadow reaches the next
    # 100-unit Z cell. Above c.z+1 all later distances remain >0.1, so the
    # caller's binary visibility is already proven to be one.
    source = source.replace('    if(abs(isect.x)<1E-4)',
                            '    if(!material && isect.x>=.1 && rp.z>grid.c.z+1.){break;}\n'
                            '    if(abs(isect.x)<1E-4)')
    source = source.replace('domarch(ro,rd,100)', 'domarch(ro,rd,100,true)')
    source = source.replace('domarch(march.rp,l,30)', 'domarch(march.rp,l,30,false)')
    # A back-facing light contributes exactly zero; its shadow cannot affect RGB.
    source = source.replace('      float shadow=step(', '      if(dotnl>0.){\n      float shadow=step(')
    source = source.replace('*(diff+spec);', '*(diff+spec);\n      }')
    return source


def lower_cpu(source):
    cpu = subprocess.run(shlex.split(os.environ.get('HOST_CC', 'cc')) + ['-E', '-P', '-x', 'c', '-'],
                         input=source, text=True, capture_output=True, check=True).stdout
    cpu = cpu.replace('sdbox(', 'sdbox2(')
    cpu = cpu.replace('sdbox2(vec3 p,vec3 s)', 'sdbox3(vec3 p,vec3 s)')
    cpu = cpu.replace('sdbox2(p+vec3(0,0,1),psize)', 'sdbox3(p+vec3(0,0,1),psize)')
    cpu = re.sub(r'sdbox2\((pt|p|pt-vec3\(0,-\.1,0\)),vec3\(', r'sdbox3(\1,vec3(', cpu)
    return PREFIX + cpu.replace('texture(iChannel0,', 'texture2D(iChannel0,') + SUFFIX


def generate():
    source = (APP / 'res/image.glsl').read_text()
    assert hashlib.sha256(source.encode()).hexdigest() == SOURCE_SHA256, 'Reference source changed'
    # Keep both original and optimized variants for independent A/B validation.
    # The interpreter needs macros expanded and overloaded names disambiguated.
    reference = lower_cpu(source)
    optimized = optimize(source)
    gpu = PREFIX + optimized + SUFFIX
    cpu = lower_cpu(optimized)
    reference_gpu = PREFIX + source + SUFFIX
    write_changed(APP / 'gen/tuner.frag', gpu)
    write_changed(APP / 'gen/tuner_cpu.frag', cpu)
    header = '/* Generated by tools/shader.py from res/image.glsl. */\n#ifndef TUNER_GLSL_H\n#define TUNER_GLSL_H\n'
    for name, value in [('tuner_vs_glsl', VS), ('tuner_fs_glsl', cpu), ('tuner_gpu_fs_glsl', gpu),
                        ('tuner_reference_fs_glsl', reference), ('tuner_reference_gpu_fs_glsl', reference_gpu)]:
        header += 'static const char *const ' + name + ' =\n'
        header += '\n'.join(json.dumps(line + '\n') for line in value.splitlines()) + ';\n'
    write_changed(APP / 'gen/tuner_glsl.h', header + '#endif\n')


def compare_ppm(paths):
    images = []
    for path in paths:
        magic, size, maximum, pixels = Path(path).read_bytes().split(b'\n', 3)
        assert magic == b'P6' and maximum == b'255'
        width, height = map(int, size.split())
        assert len(pixels) == width * height * 3, 'Incomplete frame capture'
        images.append((size, pixels))
    assert images[0][0] == images[1][0]
    a, b = images[0][1], images[1][1]
    errors = [abs(x-y) for x, y in zip(a, b)]
    changed = sum(a[i:i+3] != b[i:i+3] for i in range(0, len(a), 3))
    print(f'pixels={len(a)//3} changed={changed} RGB MAE={sum(errors)/len(errors):.6f}/255 max={max(errors)}')
    return changed == 0


if __name__ == '__main__':
    parser = argparse.ArgumentParser()
    parser.add_argument('--import-reference')
    parser.add_argument('--icon-from-rgba')
    parser.add_argument('--compare-ppm', nargs=2)
    args = parser.parse_args()
    if args.compare_ppm:
        raise SystemExit(0 if compare_ppm(args.compare_ppm) else 1)
    if args.import_reference:
        import_reference(args.import_reference)
    if args.icon_from_rgba:
        import_icon(args.icon_from_rgba)
    generate()
