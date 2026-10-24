#!/bin/sh
# Host codegen reproduction for the vendored mesa compiler front half
# (GLSL -> linked NIR; the shared, GPU-neutral part of pglcompiler).
# The EwokOS device build has NO python/bison/flex/mako, so every mesa
# build-time-generated source/header is produced HERE on the host and
# committed into the vendored tree. Run from the mesa-vendor checkout root
# (the dir containing src/). PIN = f45825a0c724d06f231a1210484fe9af71a3ffab.
#
# GPU back halves are NOT generated here: the V3D (src/broadcom) and Adreno
# (src/freedreno) generated files live with their backends under
# machines/<hw>/system/libs/{v3dcompiler,adrenocompiler}/mesa/src/ and are
# produced by the recipes documented there.
set -u

ROOT=$(cd "$(dirname "$0")" && pwd)
VENDOR="${1:-$ROOT/mesa-vendor}"
PY="${PY:-$ROOT/mesa-codegen-venv/bin/python}"
BISON="${BISON:-/opt/homebrew/opt/bison/bin/bison}"
FLEX="${FLEX:-/usr/bin/flex}"

cd "$VENDOR" || { echo "no vendor tree at $VENDOR"; exit 1; }
echo "VENDOR=$VENDOR"
echo "PY=$PY  BISON=$BISON  FLEX=$FLEX"

ok=0; fail=0
gen() { # gen <relout> <cmd...>
  out="$1"; shift
  "$@" > "$out" 2>/tmp/cg_err.txt
  if [ $? -eq 0 ] && [ -s "$out" ]; then
    echo "OK   $out ($(wc -l < "$out" | tr -d ' ') lines)"; ok=$((ok+1))
  else
    echo "FAIL $out"; sed 's/^/       /' /tmp/cg_err.txt | head -6; fail=$((fail+1))
  fi
}
genout() { # genout <relout> <cmd...>  (cmd writes the file itself)
  out="$1"; shift
  "$@" 2>/tmp/cg_err.txt
  if [ $? -eq 0 ] && [ -s "$out" ]; then
    echo "OK   $out ($(wc -l < "$out" | tr -d ' ') lines)"; ok=$((ok+1))
  else
    echo "FAIL $out"; sed 's/^/       /' /tmp/cg_err.txt | head -6; fail=$((fail+1))
  fi
}

echo "== drop 49MB nir_opt_algebraic.c (stubbed in WS2) =="
rm -f src/compiler/nir/nir_opt_algebraic.c

echo "== src/compiler (builtin_types, ir_expression_operation) =="
( cd src/compiler && genout builtin_types.h  "$PY" builtin_types_h.py builtin_types.h )
( cd src/compiler && genout builtin_types.c  "$PY" builtin_types_c.py builtin_types.c )
( cd src/compiler/glsl && gen ../ir_expression_operation.h "$PY" ir_expression_operation.py enum )

echo "== src/compiler/glsl (ir_expression_operation variants) =="
( cd src/compiler/glsl && gen ir_expression_operation_constant.h "$PY" ir_expression_operation.py constant )
( cd src/compiler/glsl && gen ir_expression_operation_strings.h  "$PY" ir_expression_operation.py strings )

echo "== src/compiler/glsl (xxd-embedded .glsl -> .h) =="
( cd src/compiler/glsl && genout float64_glsl.h "$PY" ../../util/xxd.py float64.glsl float64_glsl.h -n float64_source )
( cd src/compiler/glsl && genout cross_platform_settings_piece_all.h "$PY" ../../util/xxd.py CrossPlatformSettings_piece_all.glsl cross_platform_settings_piece_all.h -n cross_platform_settings_piece_all_header )
( cd src/compiler/glsl && genout bc1_glsl.h "$PY" ../../util/xxd.py bc1.glsl bc1_glsl.h -n bc1_source )
( cd src/compiler/glsl && genout bc4_glsl.h "$PY" ../../util/xxd.py bc4.glsl bc4_glsl.h -n bc4_source )
( cd src/compiler/glsl && genout etc2_rgba_stitch_glsl.h "$PY" ../../util/xxd.py etc2_rgba_stitch.glsl etc2_rgba_stitch_glsl.h -n etc2_rgba_stitch_source )
( cd src/compiler/glsl && genout astc_glsl.h "$PY" ../../util/xxd.py astc_decoder.glsl astc_glsl.h -n astc_source )

echo "== src/compiler/glsl (bison parser + flex lexer) =="
( cd src/compiler/glsl && genout glsl_parser.cpp "$BISON" -o glsl_parser.cpp -p _mesa_glsl_ --defines=glsl_parser.h glsl_parser.yy )
[ -s src/compiler/glsl/glsl_parser.h ] && echo "OK   glsl_parser.h ($(wc -l < src/compiler/glsl/glsl_parser.h | tr -d ' ') lines)" && ok=$((ok+1))
( cd src/compiler/glsl && genout glsl_lexer.cpp "$FLEX" -o glsl_lexer.cpp glsl_lexer.ll )

echo "== src/compiler/glsl/glcpp (bison + flex) =="
( cd src/compiler/glsl/glcpp && genout glcpp-parse.c "$BISON" -o glcpp-parse.c -p glcpp_parser_ --defines=glcpp-parse.h glcpp-parse.y )
[ -s src/compiler/glsl/glcpp/glcpp-parse.h ] && echo "OK   glcpp-parse.h ($(wc -l < src/compiler/glsl/glcpp/glcpp-parse.h | tr -d ' ') lines)" && ok=$((ok+1))
( cd src/compiler/glsl/glcpp && genout glcpp-lex.c "$FLEX" -o glcpp-lex.c glcpp-lex.l )

echo "== src/util/format (u_format_table.py, needs PyYAML) =="
( cd src/util/format && gen u_format_gen.h   "$PY" u_format_table.py u_format.yaml --enums )
( cd src/util/format && gen u_format_pack.h  "$PY" u_format_table.py u_format.yaml --header )
( cd src/util/format && gen u_format_table.c "$PY" u_format_table.py u_format.yaml )

echo "== src/util (format_srgb) =="
( cd src/util && gen format_srgb.c "$PY" format_srgb.py )

echo "==============================="
echo "codegen OK=$ok FAIL=$fail"
[ "$fail" -eq 0 ]
