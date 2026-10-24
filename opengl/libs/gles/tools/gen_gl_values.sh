#!/bin/sh
# Regenerate src/engine/gles_gl_values.h from the vendored Khronos headers.
#
# pgl_engine.c includes portablegl.h, which cannot coexist with the standard
# GLES2/gl2.h in one TU, so it cannot see the standard GL_* token values it
# must translate from.  This script re-exposes every standard GL_* integer
# token under a GLES2_GL_* name (safe alongside portablegl.h).
#
# Run from the libs/gles directory:  sh tools/gen_gl_values.sh
set -e
cd "$(dirname "$0")/.."
OUT=src/engine/gles_gl_values.h
IN="include/GLES2/gl2.h include/GLES3/gl3.h include/GLES3/gl31.h include/GLES3/gl32.h"

{
  echo "/* gles_gl_values.h - AUTO-GENERATED from the Khronos GLES2/gl2.h + GLES3/gl3.h."
  echo " * Do not edit by hand; regenerate with the script in libs/gles/tools/gen_gl_values.sh."
  echo " *"
  echo " * The engine adapter TU (pgl_engine.c) includes portablegl.h, which is"
  echo " * mutually incompatible with the standard gl2.h in one translation unit"
  echo " * (GL_NO_ERROR is a macro here but an enum member there, and several limit"
  echo " * tokens carry different values).  pgl_engine.c therefore cannot see the"
  echo " * standard GL_* tokens, yet must translate the standard values the GLES2/EGL"
  echo " * layer hands it into portablegl's own enum ordinals.  This header re-exposes"
  echo " * every standard token under a GLES2_GL_* name so it can be included safely"
  echo " * alongside portablegl.h. */"
  echo "#ifndef GLES_GL_VALUES_H"
  echo "#define GLES_GL_VALUES_H"
  echo ""
  grep -hoE '^#define[[:space:]]+GL_[A-Z0-9_]+[[:space:]]+(0x[0-9A-Fa-f]+|[0-9]+)[[:space:]]*$' $IN \
    | awk '{print "#define GLES2_"$2" "$3}' | sort -u
  echo ""
  echo "#endif /* GLES_GL_VALUES_H */"
} > "$OUT"

echo "generated $OUT: $(grep -c GLES2_GL_ "$OUT") tokens"
