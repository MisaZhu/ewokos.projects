#!/usr/bin/env python3
"""Multi-pass Shadertoy CNN shader transformer for opengl/apps/cnn.

Architecture: 6 passes (Common + Buffer A/B/C/D + Image) with 4 render targets.
- Buffer A: 32x32 ink canvas, self-feedback, mouse drawing
- Buffer B: conv1 (1->12 channels), reads A
- Buffer C: conv2 (12->24 channels), reads B
- Buffer D: FC classifier + softmax, reads A+C, self-feedback
- Image: visualization, reads all 4 buffers

The generator emits GLES2-compatible fragment shaders for each pass and a
combined header with all sources as C string literals.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import sys

APP = Path(__file__).resolve().parents[1]
RES = APP / 'res'
GEN = APP / 'gen'

# Pass names in dependency order (must match Shadertoy render order)
PASS_NAMES = ['Common', 'Buffer_A', 'Buffer_B', 'Buffer_C', 'Buffer_D', 'Image']
PASS_FILES = {
    'Common': 'common.glsl',
    'Buffer_A': 'buffer_a.glsl',
    'Buffer_B': 'buffer_b.glsl',
    'Buffer_C': 'buffer_c.glsl',
    'Buffer_D': 'buffer_d.glsl',
    'Image': 'image.glsl',
}

VS = 'attribute vec3 aPosition;\nvarying vec2 vTexCoord;\nvoid main() {\n  gl_Position = vec4(aPosition, 1.0);\n  vTexCoord = aPosition.xy * 0.5 + 0.5;\n}\n'

def write_changed(path, data):
    """Write file only if content changed (preserves mtime for make)."""
    path.parent.mkdir(parents=True, exist_ok=True)
    if isinstance(data, str):
        data = data.encode()
    if not path.exists() or path.read_bytes() != data:
        path.write_bytes(data)
        return True
    return False

def load_pass(name):
    """Load a pass source from res/."""
    path = RES / PASS_FILES[name]
    if not path.exists():
        print(f"ERROR: {path} not found. Copy from ~/Downloads/Nfy3DW_*.glsl", file=sys.stderr)
        sys.exit(1)
    return path.read_text()

def compute_hashes():
    """Compute SHA-256 hashes of all pass sources for provenance."""
    hashes = {}
    for name in PASS_NAMES:
        path = RES / PASS_FILES[name]
        if path.exists():
            hashes[name] = hashlib.sha256(path.read_bytes()).hexdigest()
    return hashes

def prefix_for_pass(name, channels):
    """Generate GLES2 uniform prefix for a pass.
    
    channels: list of (slot, source_name) tuples, e.g. [(0, 'Buffer_A'), (1, 'Buffer_B')]
    """
    lines = [
        'precision highp float;',
        'precision highp int;',
        'uniform vec3 iResolution;',
        'uniform float iTime;',
        'uniform float iTimeDelta;',
        'uniform int iFrame;',
        'uniform vec4 iMouse;',
        'uniform vec4 iDate;',
        'uniform float iFrameRate;',
    ]
    for slot, src in channels:
        lines.append(f'uniform sampler2D iChannel{slot};')
        lines.append(f'uniform vec3 iChannelResolution{slot};')
        lines.append(f'uniform float iChannelTime{slot};')
    return '\n'.join(lines) + '\n'

def suffix_for_pass(name):
    """Generate GLES2 main() wrapper for a pass."""
    if name == 'Image':
        return '\nvoid main() {\n  vec4 color;\n  mainImage(color, gl_FragCoord.xy);\n  gl_FragColor = vec4(color.rgb, 1.0);\n}\n'
    else:
        # Buffer passes write to gl_FragColor
        return '\nvoid main() {\n  vec4 color;\n  mainImage(color, gl_FragCoord.xy);\n  gl_FragColor = color;\n}\n'

def transform_glsl(source, is_common=False):
    """Transform Shadertoy GLSL to GLES2-compatible source.
    
    - Replace texture() with texture2D()
    - Expand #include "Common" (handled by concatenation)
    - Handle Shadertoy-specific extensions
    """
    if is_common:
        # Common pass is prepended to all others, no main() wrapper
        return source
    
    # texture() -> texture2D() for GLES2
    source = re.sub(r'\btexture\s*\(', 'texture2D(', source)
    
    # textureLod -> texture2D with bias (approximation)
    source = re.sub(r'\btextureLod\s*\(\s*([^,]+),\s*([^,]+),\s*[^)]+\)', 
                    r'texture2D(\1, \2)', source)
    
    return source

def generate_pass(name, common_source, channel_map):
    """Generate a single pass fragment shader."""
    source = load_pass(name)
    
    if name == 'Common':
        # Common is included in all other passes, not compiled standalone
        return transform_glsl(source, is_common=True)
    
    # Prepend Common to the pass
    combined = common_source + '\n' + transform_glsl(source)
    
    # Add prefix and suffix
    channels = channel_map.get(name, [])
    prefix = prefix_for_pass(name, channels)
    suffix = suffix_for_pass(name)
    
    return prefix + combined + suffix

def generate():
    """Generate all pass shaders and the combined header."""
    print("Generating multi-pass CNN shaders...")
    
    # Load Common first (prepended to all passes)
    common_source = load_pass('Common')
    common_transformed = transform_glsl(common_source, is_common=True)
    
    # Channel mapping from metadata (Shadertoy iChannel bindings)
    # Image: ch0=BufA, ch1=BufB, ch2=BufC, ch3=BufD
    # Buffer A: ch0=BufA (self-feedback)
    # Buffer B: ch0=BufA
    # Buffer C: ch0=BufB
    # Buffer D: ch0=BufA, ch2=BufC, ch3=BufD (self-feedback)
    channel_map = {
        'Image': [(0, 'Buffer_A'), (1, 'Buffer_B'), (2, 'Buffer_C'), (3, 'Buffer_D')],
        'Buffer_A': [(0, 'Buffer_A')],
        'Buffer_B': [(0, 'Buffer_A')],
        'Buffer_C': [(0, 'Buffer_B')],
        'Buffer_D': [(0, 'Buffer_A'), (2, 'Buffer_C'), (3, 'Buffer_D')],
    }
    
    # Generate each pass
    generated = {}
    for name in PASS_NAMES:
        if name == 'Common':
            continue  # Common is included, not standalone
        
        frag_source = generate_pass(name, common_transformed, channel_map)
        frag_name = name.lower().replace('_', '')
        out_path = GEN / f'{frag_name}.frag'
        write_changed(out_path, frag_source)
        generated[name] = frag_source
        print(f"  {name}: {len(frag_source)} chars -> {out_path.name}")
    
    # Generate combined header with all sources as C strings
    header_lines = [
        '/* Generated by tools/shader.py from res/ GLSL files */',
        '/* CNN Letter Recognizer - Original implementation */',
        '/* Inspired by Shadertoy Nfy3DW by rickyyy */',
        '#ifndef CNN_GLSL_H',
        '#define CNN_GLSL_H',
        '',
    ]
    
    # Vertex shader (shared by all passes)
    header_lines.append('static const char *const cnn_vs_glsl =')
    header_lines.append('\n'.join(json.dumps(line + '\n') for line in VS.splitlines()) + ';')
    header_lines.append('')
    
    # Fragment shaders for each pass
    for name in ['Buffer_A', 'Buffer_B', 'Buffer_C', 'Buffer_D', 'Image']:
        var_name = f'cnn_{name.lower().replace("_", "")}_fs_glsl'
        source = generated[name]
        header_lines.append(f'static const char *const {var_name} =')
        header_lines.append('\n'.join(json.dumps(line + '\n') for line in source.splitlines()) + ';')
        header_lines.append('')
    
    header_lines.append('#endif /* CNN_GLSL_H */')
    
    header_path = GEN / 'cnn_glsl.h'
    write_changed(header_path, '\n'.join(header_lines))
    print(f"  Header: {header_path}")
    
    # Update source.json with hashes
    hashes = compute_hashes()
    meta_path = RES / 'source.json'
    if meta_path.exists():
        meta = json.loads(meta_path.read_text())
    else:
        meta = {}
    
    meta.update({
        'url': 'https://www.shadertoy.com/view/Nfy3DW',
        'title': 'Handwritten Letter Recognizer',
        'author': 'rickyyy',
        'description': 'Draw an A-Z letter and watch a CNN recognize it in real time. Displays convolution, pooling, flattening and the final prediction entirely in GLSL.',
        'license': 'Shadertoy default license (CC BY-NC-SA 3.0 unless specified otherwise)',
        'attribution': 'Ported to EwokOS opengl with attribution to original author',
        'pass_hashes': hashes,
        'architecture': {
            'passes': 6,
            'render_targets': 4,
            'feedback_loops': ['Buffer_A (self)', 'Buffer_D (self)'],
            'cnn_layers': ['conv1 (1->12)', 'conv2 (12->24)', 'fc (26 classes)'],
        },
    })
    
    write_changed(meta_path, json.dumps(meta, ensure_ascii=False, indent=2) + '\n')
    print(f"  Metadata: {meta_path}")
    print("Done.")

def verify():
    """Verify source files exist and report sizes."""
    print("Verifying source files...")
    total = 0
    for name in PASS_NAMES:
        path = RES / PASS_FILES[name]
        if path.exists():
            size = path.stat().st_size
            total += size
            print(f"  {name}: {size:,} bytes")
        else:
            print(f"  {name}: MISSING ({path})")
    print(f"Total: {total:,} bytes")
    return total > 0

if __name__ == '__main__':
    parser = argparse.ArgumentParser(description='Multi-pass CNN shader generator')
    parser.add_argument('--verify', action='store_true', help='Verify source files exist')
    parser.add_argument('--clean', action='store_true', help='Remove generated files')
    args = parser.parse_args()
    
    if args.clean:
        for f in GEN.glob('*'):
            f.unlink()
        print("Cleaned gen/")
        sys.exit(0)
    
    if args.verify:
        sys.exit(0 if verify() else 1)
    
    if not verify():
        print("\nERROR: Source files missing. Copy from browser downloads:", file=sys.stderr)
        print("  cp ~/Downloads/Nfy3DW_1_Common.glsl res/common.glsl", file=sys.stderr)
        print("  cp ~/Downloads/Nfy3DW_4_Buffer_A.glsl res/buffer_a.glsl", file=sys.stderr)
        print("  cp ~/Downloads/Nfy3DW_2_Buffer_B.glsl res/buffer_b.glsl", file=sys.stderr)
        print("  cp ~/Downloads/Nfy3DW_5_Buffer_C.glsl res/buffer_c.glsl", file=sys.stderr)
        print("  cp ~/Downloads/Nfy3DW_3_Buffer_D.glsl res/buffer_d.glsl", file=sys.stderr)
        print("  cp ~/Downloads/Nfy3DW_0_Image.glsl res/image.glsl", file=sys.stderr)
        sys.exit(1)
    
    generate()
