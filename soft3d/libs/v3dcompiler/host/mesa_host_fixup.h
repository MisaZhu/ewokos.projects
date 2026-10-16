/* WS6 host-oracle config fixup.
 *
 * Force-included AFTER the device mesa_config.h (Makefile.host: -include $(CFG)
 * -include this). It only reverses the macros whose GCC builtins Apple clang
 * does not implement, so mesa takes its portable fallbacks:
 *
 *   __builtin_add_overflow_p / __builtin_sub_overflow_p are GCC-only predicate
 *   builtins; clang 21 has __builtin_add_overflow but not the _p variants.
 *   util/u_overflow.h guards both with #ifdef HAVE___BUILTIN_{ADD,SUB}_OVERFLOW_P
 *   and provides static-inline portable implementations when they are absent.
 *
 * Everything else in the device config (math constants, PRIxPTR, endian, the
 * other __builtin_* clang does support) probed clean and is left as-is.
 */
#ifndef V3DCOMPILER_MESA_HOST_FIXUP_H
#define V3DCOMPILER_MESA_HOST_FIXUP_H

#undef HAVE___BUILTIN_ADD_OVERFLOW_P
#undef HAVE___BUILTIN_SUB_OVERFLOW_P

/* The device mesa_config.h defines getc(stream)->fgetc(stream) because the
 * EwokOS SDK <stdio.h> declares fgetc but not getc, and the flex-generated
 * glsl_lexer.cpp YY_INPUT macro calls getc(yyin). macOS <stdio.h> provides a
 * real getc(), so drop the shim macro and include the genuine header; the
 * lexer then binds getc to libc. */
#undef getc
#include <stdio.h>

#endif /* V3DCOMPILER_MESA_HOST_FIXUP_H */
