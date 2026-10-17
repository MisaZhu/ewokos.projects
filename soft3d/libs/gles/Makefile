PROJS_ROOT_DIR=../../..
include $(PROJS_ROOT_DIR)/make.inc

BUILD_DIR = $(EWOKOS_SYS_DIR)/build_$(ARCH)/$(HW)

# portablegl and v3dcompiler live UNDER this dir and are folded into
# libGLESv2.a, so an app links only -lGLESv2 -lEGL (+ libglcommon + system
# libs): the portablegl engine and the on-device mesa GLSL->QPU compiler are
# invisible to it.  Their archives are built as PRIVATE intermediates under
# .gles_priv/ and are never installed into $(BUILD_DIR)/lib.
PRIV = $(BUILD_DIR)/lib/.gles_priv

# -Iinclude            : the standard GLES2/GLES3/EGL/KHR headers this lib exports
# -Iportablegl/include : ONLY for src/engine/pgl_engine.c (the single adapter
#                        TU allowed to include portablegl.h)
# $(SDK_DIR)/include (added by make.inc) : <x/xwin.h> + <graph/graph.h> for EGL
CFLAGS += -Iinclude -Isrc/engine -Isrc/glsl -Iportablegl/include

# portablegl.h is preprocessed identically here and in the private
# libportablegl.a only if the PGL_G3D gate matches, so mirror it: raspi5 exports
# PGL_G3D=yes, which is how libportablegl.a is built.  A mismatch would change
# the glContext the engine TU sees vs. the one compiled into the archive.
ifeq ($(PGL_G3D),yes)
CFLAGS += -DPGL_G3D
endif

# libGLESv2.a: the engine adapter (portablegl bridge) + the gl* entry points,
# merged with the portablegl engine and (on PGL_G3D targets) the mesa compiler.
# The engine object MUST live in exactly one archive; libEGL.a references its
# gles_engine_* symbols and resolves them from libGLESv2.a at app link time.
LIB_GLESV2_OBJS = \
	$(ARCH)/src/engine/pgl_engine.o \
	$(ARCH)/src/gles2/gles2_api.o \
	$(ARCH)/src/gles2/gles2_shader.o \
	$(ARCH)/src/glsl/glsl_lex.o \
	$(ARCH)/src/glsl/glsl_parse.o \
	$(ARCH)/src/glsl/glsl_exec.o \
	$(ARCH)/src/glsl/glsl_program.o

# libEGL.a: the egl* entry points.
LIB_EGL_OBJS = \
	$(ARCH)/src/egl/egl_api.o

GLESV2 = $(BUILD_DIR)/lib/libGLESv2.a
EGLIB  = $(BUILD_DIR)/lib/libEGL.a

# Private intermediates merged into libGLESv2.a.  v3dcompiler (the mesa
# GLSL->QPU compiler) folds in only on PGL_G3D targets; elsewhere it is a no-op
# and portablegl keeps its CPU/NEON rasterizer.
PGL_PRIV   = $(PRIV)/libportablegl.a
V3D_PRIV   = $(PRIV)/libv3dcompiler.a
MERGE_LIBS = $(PGL_PRIV)
ifeq ($(PGL_G3D),yes)
MERGE_LIBS += $(V3D_PRIV)
endif

all: $(GLESV2) $(EGLIB)
	@mkdir -p $(BUILD_DIR)/include
	cp -r include/* $(BUILD_DIR)/include
	@# the folded-in backends stay invisible: never leave public copies
	@rm -f $(BUILD_DIR)/lib/libportablegl.a $(BUILD_DIR)/lib/libv3dcompiler.a

# Merge gles's own objects + the private portablegl/v3dcompiler archives into a
# single libGLESv2.a with an ar MRI script.  A lone archive is rescanned by ld
# until no new member resolves, so mesa's internal circular refs settle without
# an app-side --start-group; mesa's libc/libm/cxx refs are forward refs to the
# system archives that follow libGLESv2.a on the app link line.  Uses $(AR)
# (the cross ar) exclusively - never mix host/cross ar on one archive.
$(GLESV2): $(LIB_GLESV2_OBJS) $(MERGE_LIBS)
	@mkdir -p $(dir $(GLESV2))
	@rm -f $(GLESV2) gles_merge.mri
	@{ echo "CREATE $(GLESV2)"; \
	   echo "ADDMOD $(LIB_GLESV2_OBJS)"; \
	   for a in $(MERGE_LIBS); do echo "ADDLIB $$a"; done; \
	   echo "SAVE"; echo "END"; } > gles_merge.mri
	$(AR) -M < gles_merge.mri
	$(AR) s $(GLESV2)
	@rm -f gles_merge.mri

$(EGLIB): $(LIB_EGL_OBJS)
	@mkdir -p $(dir $(EGLIB))
	$(AR) rcs $(EGLIB) $(LIB_EGL_OBJS)

# portablegl builds after v3dcompiler so pgl_g3d_shader.c sees the
# geom_raster_cs.h that v3dcompiler stages into $(BUILD_DIR)/include.  The
# dependency is declared only on PGL_G3D targets (v3dcompiler is a no-op else).
ifeq ($(PGL_G3D),yes)
$(PGL_PRIV): $(V3D_PRIV)
endif
$(PGL_PRIV):
	@mkdir -p $(PRIV)
	$(MAKE) -C portablegl PORTABLEGL=$(PGL_PRIV)

$(V3D_PRIV):
	@mkdir -p $(PRIV)
	$(MAKE) -C v3dcompiler LIB=$(V3D_PRIV)

# App link order (each lib may reference the one to its right):
#   -lEGL -lGLESv2  (+ libx/libgraph/libc)  -- portablegl & mesa are inside -lGLESv2
$(ARCH)/src/%.o: src/%.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

.PHONY: all clean

clean:
	-$(MAKE) -C portablegl clean
	-$(MAKE) -C v3dcompiler clean
	rm -rf $(ALL_ARCH_DIRS)
	rm -f src/*.o
	rm -f $(GLESV2) $(EGLIB) gles_merge.mri
	rm -rf $(PRIV)
	rm -f $(BUILD_DIR)/lib/libportablegl.a $(BUILD_DIR)/lib/libv3dcompiler.a
