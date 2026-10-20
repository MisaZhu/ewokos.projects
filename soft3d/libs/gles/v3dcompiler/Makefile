PROJS_ROOT_DIR=../../../..
include $(PROJS_ROOT_DIR)/make.inc

BUILD_DIR = $(EWOKOS_SYS_DIR)/build_$(ARCH)/$(HW)
LIB       = $(BUILD_DIR)/lib/libv3dcompiler.a

.PHONY: all clean

ifneq ($(PGL_G3D),yes)

# libv3dcompiler.a is the on-device mesa GLSL->NIR->VIR->QPU compiler used by
# portablegl's GPU 3D backend. It is built only when the target exports the
# PGL_G3D capability (a VideoCore V3D pipeline); everywhere else it is a no-op so
# portablegl keeps its CPU/NEON rasterizer and links no GPU compiler.
all:
	@echo "v3dcompiler: skipped (PGL_G3D=$(PGL_G3D) != yes; no V3D GPU compiler)"

clean:
	@true

else

# The platform port dir supplies the EwokOS build config (mesa_config.h and the
# assert.h/syslog.h shims). It is exported as V3D_PORT_DIR by the owning
# machine's system/make.inc, so this shared Makefile never names a board.
ifeq ($(V3D_PORT_DIR),)
$(error v3dcompiler: V3D_PORT_DIR is empty - the platform must export it from machines/<hw>/system/make.inc, pointing at the bsp g3d port dir holding mesa_config.h)
endif

# === include order ==========================================================
# $(V3D_PORT_DIR) MUST come first: its assert.h shim uses #include_next to wrap
# the SDK <assert.h> (adding C11 static_assert) and its syslog.h shim shadows the
# absent system one. make.inc already appended "-I include -I $(SDK_DIR)/include",
# so prepend with ':=' to win the search. This order is the one validated against
# ast_to_hir.cpp / glsl_to_nir.cpp / nir_to_vir.c / ralloc.c (see VENDORED.txt).
# compiler/nir is on the path so the bare "nir_defines.h" (gl_nir_linker.h) and
# "nir.h"/"nir_search.h" (generated v3d_nir_lower_algebraic.c) includes resolve,
# exactly as mesa's inc_compiler_nir does for those targets. compiler/glsl is on
# the path for the same reason (mesa's inc_glsl = include_directories('glsl')):
# the WS2-entry driver v3dcompiler_glsl.cpp lives at the lib root, not inside
# compiler/glsl, so it cannot reach the frontend headers (glsl_parser_extras.h,
# standalone_scaffolding.h, gl_nir_linker.h, builtin_functions.h, linker_util.h)
# by sibling quote-include the way the vendored glsl/*.cpp do.
V3D_INC = \
	-I$(V3D_PORT_DIR) \
	-Imesa/include \
	-Imesa/src \
	-Imesa/src/broadcom \
	-Imesa/src/compiler \
	-Imesa/src/compiler/glsl \
	-Imesa/src/compiler/nir \
	-Imesa/src/mesa
V3D_FORCEINC = -include $(V3D_PORT_DIR)/mesa_config.h

# -w: mesa is vendored third-party code; the -Wall/-Wextra/-pedantic flood from
#     make.rule is pure noise here. -std=gnu11 / -std=gnu++17 are appended LAST
#     so they override make.rule's -std=c99 / -std=c++14 (GCC honours the last
#     -std). gnu11/gnu++17 give mesa the statement-expressions, typeof and C++17
#     features it relies on.
CFLAGS   := $(V3D_INC) $(V3D_FORCEINC) $(CFLAGS) -w -std=gnu11
CXXFLAGS := $(V3D_INC) $(V3D_FORCEINC) $(CXXFLAGS) -w -std=gnu++17

# === sources ================================================================
S = mesa/src

# util (C): exclude the disk-cache / DB / socket / dlopen / backtrace / x86-asm
# / XML / android / video / process-info files. All are Linux- or host-only and
# none is reachable from the GLSL->NIR->VIR->QPU path; any symbol they would
# have provided is stubbed at link time (WS2-link) if still referenced.
UTIL_EXCL = \
	$(S)/util/build_id.c \
	$(S)/util/cache_ops_aarch64.c $(S)/util/cache_ops_null.c \
	$(S)/util/cache_ops_x86.c $(S)/util/cache_ops_x86_clflushopt.c \
	$(S)/util/disk_cache.c $(S)/util/disk_cache_os.c \
	$(S)/util/fossilize_db.c \
	$(S)/util/mesa_cache_db.c $(S)/util/mesa_cache_db_multipart.c \
	$(S)/util/os_file_notify.c $(S)/util/os_socket.c \
	$(S)/util/parson.c $(S)/util/streaming-load-memcpy.c \
	$(S)/util/thread_sched.c \
	$(S)/util/u_debug_stack.c $(S)/util/u_debug_symbol.c $(S)/util/u_dl.c \
	$(S)/util/u_process.c $(S)/util/u_shader_variant_cache.c \
	$(S)/util/u_sync_provider.c \
	$(S)/util/vl_zscan_data.c $(S)/util/xmlconfig.c
UTIL_C = $(filter-out $(UTIL_EXCL), $(wildcard $(S)/util/*.c))

# util (C++): none. u_qsort.cpp needs C++ <thread> (absent from EWOK_STL); its
# only exported symbol, util_tls_qsort_r (the util_qsort_r fallback in
# u_qsort.h), is provided TLS-based in v3dcompiler_ewokos.c instead. The ASTC
# LUT/codec tables and the android backtrace symbolizer are host/codec-only.
UTIL_CXX =

# util/format codecs (u_format_table.c references their fetch/unpack fns).
# u_format_tests.c is a standalone test harness with its own main().
FORMAT_C = $(filter-out $(S)/util/format/u_format_tests.c, $(wildcard $(S)/util/format/*.c))

# blake3: portable + NEON dispatch only (no x86 avx2/avx512/sse2/sse41).
BLAKE3_C = $(S)/util/blake3/blake3.c $(S)/util/blake3/blake3_dispatch.c \
	$(S)/util/blake3/blake3_portable.c $(S)/util/blake3/blake3_neon.c

# compiler top level. builtin_types.c is the GENERATED builtin-name table; the
# real glsl/builtin_types.cpp defines the type instances (different symbols, so
# both are needed and do not collide).
COMPILER_C = $(S)/compiler/builtin_types.c $(S)/compiler/glsl_types.c \
	$(S)/compiler/shader_enums.c

# NIR: every pass. nir_opt_algebraic.c (~49 MB generated) is deliberately not
# vendored; its 6 entry points are no-op stubbed in v3dcompiler_stubs.c
# (correctness is unaffected - algebraic rewriting is an optimisation only).
# nir_stub.c is Microsoft's empty no-op nir_print_shader() - an ALTERNATIVE to
# the real printer in nir_print.c. Both match the wildcard and both define
# nir_print_shader strongly, so the stub is dropped and nir_print.c wins (it also
# supplies nir_print_shader_annotated, which the stub lacks).
NIR_C = $(filter-out $(S)/compiler/nir/nir_stub.c, $(wildcard $(S)/compiler/nir/*.c))

# GLSL frontend (C++): exclude the standalone CLI driver (main.cpp/standalone.cpp
# have main()) and the shader disk-cache / serialize pair (host-only). Keeps
# ast_to_hir, glsl_to_nir, the bison/flex parser+lexer, the linker,
# builtin_functions and all ir_*/lower_*/opt_* passes.
# standalone_scaffolding.cpp IS built: it is mesa's own context-free stub set for
# running the GLSL compiler with no real GL context (WS2-link). It defines the
# _mesa_new_shader / _mesa_problem / _mesa_reference_shader_program_data /
# _mesa_shader_debug / _mesa_delete_shader / _mesa_delete_linked_shader and
# _mesa_program_state_{string,flags} stubs, plus the initialize_context_to_defaults
# / standalone_create_shader_program helpers the v3d_compile_glsl() entry reuses.
GLSL_EXCL = \
	$(S)/compiler/glsl/main.cpp $(S)/compiler/glsl/standalone.cpp \
	$(S)/compiler/glsl/shader_cache.cpp $(S)/compiler/glsl/serialize.cpp
GLSL_CXX = $(filter-out $(GLSL_EXCL), $(wildcard $(S)/compiler/glsl/*.cpp))

# GLSL NIR link/lower helpers (C).
GLSL_C = $(wildcard $(S)/compiler/glsl/*.c)

# glcpp preprocessor library. glcpp.c is the standalone driver (has main()) and
# pp_standalone_scaffolding.c is its support; libglcpp = lex + parse + pp.c.
GLCPP_C = $(S)/compiler/glsl/glcpp/glcpp-lex.c \
	$(S)/compiler/glsl/glcpp/glcpp-parse.c $(S)/compiler/glsl/glcpp/pp.c

# broadcom V3D backend: NIR->VIR->QPU (+ the generated v3d_nir_lower_algebraic.c
# and the v71 packet packing that qpu_pack.c / v3d_packing.c rely on).
BCOMP_C = $(wildcard $(S)/broadcom/compiler/*.c)
BQPU_C  = $(wildcard $(S)/broadcom/qpu/*.c)
# common: tiling + debug. v3d_device_info.c queries the GPU via drm ioctls
# (drm-uapi/v3d_drm.h) and v3d_util.c needs xf86drm.h - neither is reachable
# from an app-side compile (only the g2dd daemon may touch the V3D drm device),
# so the v3d_device_info for V3D 7.1 is built by hand in the WS2-entry glue.
# v3d_submit_util.c is likewise drm-only. v3d_debug.c is NOT: it is drm-free and
# supplies v3d_mesa_debug (default 0) + v3d_debug_flag_for_shader_stage(), the two
# V3D_DEBUG symbols nir_to_vir.c / vir_to_qpu.c reference (WS2-link).
BCOMMON_C = $(S)/broadcom/common/v3d_tiling.c $(S)/broadcom/common/v3d_debug.c

# mesa/program: the symbol table (GLSL parser scopes) plus prog_parameter.c (the
# gl_program_parameter_list that gl_nir_link_uniforms.c fills). prog_parameter.c is
# the real upstream impl; its only external calls (_mesa_problem and
# _mesa_program_state_{string,flags}) are satisfied by standalone_scaffolding.cpp.
MESAPROG_C = $(S)/mesa/program/symbol_table.c $(S)/mesa/program/prog_parameter.c

# mesa/main: headers are vendored but the GL-context .c files are not, so the
# _mesa_* functions the compiler references are stubbed (WS2-link). The one
# exception is extensions_table.c: a dependency-free data table (the EXT(...) list
# in extensions_table.h) defining _mesa_extension_table for the parser's #extension
# handling.
MAINEXT_C = $(S)/mesa/main/extensions_table.c

# c11 compatibility layer: mesa's own portable implementations of the C11 threads
# API (thrd_*/cnd_*/mtx_*/tss_*/call_once) on top of pthreads, and of c23
# timespec_get on top of clock_gettime. Both are platform-independent (the EwokOS
# SDK provides pthread_* and clock_gettime), so they belong in the compiler rather
# than the platform glue. threads_win32.c is the Windows counterpart, not built.
C11_C = $(S)/c11/impl/threads_posix.c $(S)/c11/impl/time.c

# Portable vendoring stubs (the algebraic no-ops that are pure optimisations,
# _mesa_error_no_memory, the GLSL #include cursor trio). Platform-independent.
STUBS_C = v3dcompiler_stubs.c

# EwokOS-only glue (util_get_process_name, util_tls_qsort_r, mkstemps,
# pthread_getcpuclockid, open_memstream): the SDK/libc gaps libv3dcompiler.a
# needs, archived in so an app links the compiler self-contained. A glibc/macOS
# host supplies these symbols, so this is the only EwokOS-specific object.
EWOKOS_GLUE_C = v3dcompiler_ewokos.c

# === WS2-entry: the v3d_compile_glsl() driver ================================
# The entry point is split across C and C++ on purpose:
#   v3dcompiler_glsl.cpp  (C++) - runs the GLSL frontend (preprocess/parse/HIR/
#                          NIR/link, all C++-linkage) and re-packs the QPU words
#                          into the public little-endian u32 stream.
#   v3dcompiler_backend.c (C)   - runs the broadcom NIR->VIR->QPU backend and
#                          hand-builds the V3D 7.1 v3d_device_info. Kept C
#                          because v3d_compiler.h cannot be included from C++
#                          (it pulls <type_traits> in via util/enum_operators.h).
#   v3dcompiler_algebraic.c (C) - the focused nir_opt_algebraic() that lowers
#                          fdiv/fsqrt/fpow/fmod/frem/ffract/flrp/ffma into
#                          backend-supported primitives (the generated generic
#                          pass is not vendored; the backend abort()s on those
#                          opcodes otherwise).
# All three are platform-independent.
ENTRY_C   = v3dcompiler_backend.c v3dcompiler_algebraic.c v3dcompiler_texlayout.c
ENTRY_CXX = v3dcompiler_glsl.cpp

SRCS_C   = $(UTIL_C) $(FORMAT_C) $(BLAKE3_C) $(COMPILER_C) $(NIR_C) \
	$(GLSL_C) $(GLCPP_C) $(BCOMP_C) $(BQPU_C) $(BCOMMON_C) $(MESAPROG_C) \
	$(MAINEXT_C) $(C11_C) $(STUBS_C) $(EWOKOS_GLUE_C) $(ENTRY_C)
SRCS_CXX = $(UTIL_CXX) $(GLSL_CXX) $(ENTRY_CXX)

# Objects mirror the source tree under $(ARCH)/ via make.rule's implicit rules
# ($(ARCH)/%.o: %.c and : %.cpp), which mkdir -p the output dir first.
OBJS = $(SRCS_C:%.c=$(ARCH)/%.o) $(SRCS_CXX:%.cpp=$(ARCH)/%.o)

all: $(LIB)

$(LIB): $(OBJS)
	@mkdir -p $(dir $(LIB))
	$(AR) rcs $(LIB) $(OBJS)
	@mkdir -p $(BUILD_DIR)/include
	cp -r include/* $(BUILD_DIR)/include/

clean:
	rm -rf $(ALL_ARCH_DIRS)
	rm -f $(LIB)

endif
