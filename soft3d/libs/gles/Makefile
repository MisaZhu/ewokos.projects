PROJS_ROOT_DIR=../../..
include $(PROJS_ROOT_DIR)/make.inc

BUILD_DIR = $(EWOKOS_SYS_DIR)/build_$(ARCH)/$(HW)

# Object dir is keyed by $(HW), not just $(ARCH).  -DPGL_G3D is decided by the
# machine (raspi5/raspix export PGL_G3D=yes; virt does not), and pgl_engine.c --
# plus the portablegl.h it includes -- preprocess DIFFERENTLY under the flag (the
# GPU bridge code and a different glContext layout).  A shared $(ARCH)/src/*.o let
# one machine reuse the other's stale object: a virt build would fold a raspi5
# GPU-shaped pgl_engine.o into its CPU-only libGLESv2.a (and vice versa), so
# switching machine required a full `make clean` to recover.  Encoding $(HW) in
# the path gives every machine its own objects (mirroring BUILD_DIR's
# build_$(ARCH)/$(HW) layout); switching HW is then always a fresh compile and
# the cpu/gpu trees coexist without polluting each other.
OBJDIR = $(ARCH)/$(HW)

# portablegl and pglcompiler live UNDER this dir and are folded into
# libGLESv2.a, so an app links only -lGLESv2 -lEGL (+ libglcommon + system
# libs): the portablegl engine and the on-device mesa GLSL->GPU compiler are
# invisible to it.  Their archives are built as PRIVATE intermediates under
# .gles_priv/ and are never installed into $(BUILD_DIR)/lib.
PRIV = $(BUILD_DIR)/lib/.gles_priv

# -Iinclude            : the standard GLES2/GLES3/EGL/KHR headers this lib exports
# -Iportablegl/include : ONLY for src/engine/pgl_engine.c (the single adapter
#                        TU allowed to include portablegl.h)
# $(SDK_DIR)/include (added by make.inc) : <x/xwin.h> + <graph/graph.h> for EGL
CFLAGS += -Iinclude -Isrc/engine -Isrc/glsl -Iportablegl/include

# portablegl.h is preprocessed identically here and in the private
# libportablegl.a only if the PGL_G3D gate matches, so mirror it: the machines
# with a V3D pipeline (raspi5, raspix) export PGL_G3D=yes, which is how
# libportablegl.a is built.  A mismatch would change the glContext the engine TU
# sees vs. the one compiled into the archive.
ifeq ($(PGL_G3D),yes)
CFLAGS += -DPGL_G3D
endif

# pgl_engine.c is the sole adapter TU that includes portablegl's headers (see the
# -Iportablegl/include note above), so give the SOURCE-tree portablegl/include
# priority over the copy installed under $(SDK_DIR)/include.  make.inc puts
# -I$(SDK_DIR)/include ahead of -Iportablegl/include, and that install is only a
# side effect of building libportablegl.a -- so a header-ONLY change to portablegl
# (e.g. the PGL_G3D_BLEND_* codes) leaves the SDK copy stale while the .a stays up
# to date, and pgl_engine.c would compile against the stale header and fail with
# 'PGL_G3D_BLEND_OPAQUE undeclared'.  Prepending here makes the engine always see
# the current source headers, which is exactly what libportablegl.a compiles
# against too, so the glContext layout stays identical on both sides.
# (portablegl/include holds only the portablegl/ subdir, so this cannot shadow
# the EGL/GLES/system headers the other TUs need.)
$(OBJDIR)/src/engine/pgl_engine.o: CFLAGS := -Iportablegl/include $(CFLAGS)

# libGLESv2.a: the engine adapter (portablegl bridge) + the gl* entry points,
# merged with the portablegl engine and (on PGL_G3D targets) the mesa compiler.
# The engine object MUST live in exactly one archive; libEGL.a references its
# gles_engine_* symbols and resolves them from libGLESv2.a at app link time.
LIB_GLESV2_OBJS = \
	$(OBJDIR)/src/engine/pgl_engine.o \
	$(OBJDIR)/src/gles2/gles2_api.o \
	$(OBJDIR)/src/gles2/gles2_shader.o \
	$(OBJDIR)/src/glsl/glsl_lex.o \
	$(OBJDIR)/src/glsl/glsl_parse.o \
	$(OBJDIR)/src/glsl/glsl_exec.o \
	$(OBJDIR)/src/glsl/glsl_program.o

# libEGL.a: the egl* entry points.
LIB_EGL_OBJS = \
	$(OBJDIR)/src/egl/egl_api.o

GLESV2 = $(BUILD_DIR)/lib/libGLESv2.a
EGLIB  = $(BUILD_DIR)/lib/libEGL.a

# Private intermediates merged into libGLESv2.a.  The on-device GLSL compiler
# folds in only on PGL_G3D targets, as two archives: the shared mesa GLSL->NIR
# frontend (pglcompiler/) and the machine's GPU compiler backend
# ($(PGL_G3D_COMPILER_DIR), NIR->ISA + wire-blob serializer, exported by the
# machine make.inc).  Elsewhere both are no-ops and portablegl keeps its
# CPU/NEON rasterizer.
PGL_PRIV   = $(PRIV)/libportablegl.a
FE_PRIV    = $(PRIV)/libpglcompiler_fe.a
BE_PRIV    = $(PRIV)/libpglcompiler_be.a
MERGE_LIBS = $(PGL_PRIV)
ifeq ($(PGL_G3D),yes)
ifeq ($(PGL_G3D_COMPILER_DIR),)
$(error gles: PGL_G3D=yes but PGL_G3D_COMPILER_DIR is empty - machines/<hw>/system/make.inc must export the machine GPU compiler backend dir)
endif
MERGE_LIBS += $(FE_PRIV) $(BE_PRIV)
endif

all: $(GLESV2) $(EGLIB)
	@mkdir -p $(BUILD_DIR)/include
	cp -r include/* $(BUILD_DIR)/include
	@# the folded-in backends stay invisible: never leave public copies
	@rm -f $(BUILD_DIR)/lib/libportablegl.a $(BUILD_DIR)/lib/libv3dcompiler.a $(BUILD_DIR)/lib/libpglcompiler*.a

# Merge gles's own objects + the private portablegl/compiler archives into a
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

# The private archives are built by recursive sub-makes.  A plain file target
# here cannot see the sub-make's own source dependencies, so once the archive
# exists make treats it as up to date and NEVER re-invokes the sub-make: editing a
# portablegl/compiler source (or flipping -DPGL_G3D) left the STALE archive to
# be re-merged into libGLESv2.a, producing undefined-reference link errors for
# symbols that exist in source (e.g. pgl_g3d_set_blend,
# pgl_g3d_shader_geom_inst_lit).  Adding a FORCE prerequisite makes each
# delegation recipe run every time so the sub-make can do the real up-to-date
# check; because the private archives are still REAL files, make re-stats them
# after the recipe, so their mtime changes only when the sub-make actually
# rewrote the archive -- and $(GLESV2) re-merges only then (a no-op rebuild does
# not re-merge, so apps do not relink spuriously).
.PHONY: FORCE
FORCE:

# Build order on PGL_G3D targets: frontend -> machine backend -> portablegl.
# The backend compiles against the frontend's mesa headers (PGLC_FE_DIR), and
# pgl_g3d_shader.c needs the pglcompiler.h / geom_raster_cs.h the frontend
# stages into $(BUILD_DIR)/include.  Declared only on PGL_G3D targets (the
# compiler sub-makes are no-ops else).
ifeq ($(PGL_G3D),yes)
$(PGL_PRIV): $(BE_PRIV)
$(BE_PRIV): $(FE_PRIV)
endif
$(PGL_PRIV): FORCE
	@mkdir -p $(PRIV)
	$(MAKE) -C portablegl PORTABLEGL=$(PGL_PRIV)

$(FE_PRIV): FORCE
	@mkdir -p $(PRIV)
	$(MAKE) -C pglcompiler LIB=$(FE_PRIV)

# The machine GPU compiler backend lives in the machine tree, outside this
# project; it is driven from here because it depends on the staged SDK and on
# the frontend sources, which only exist at `make projs` time.
$(BE_PRIV): FORCE
	@mkdir -p $(PRIV)
	$(MAKE) -C $(PGL_G3D_COMPILER_DIR) LIB=$(BE_PRIV) PGLC_FE_DIR=$(abspath pglcompiler)

# App link order (each lib may reference the one to its right):
#   -lEGL -lGLESv2  (+ libx/libgraph/libc)  -- portablegl & mesa are inside -lGLESv2
$(OBJDIR)/src/%.o: src/%.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

.PHONY: all clean

clean:
	-$(MAKE) -C portablegl clean
	-$(MAKE) -C pglcompiler clean
ifeq ($(PGL_G3D),yes)
	-$(MAKE) -C $(PGL_G3D_COMPILER_DIR) LIB=$(BE_PRIV) clean
endif
	rm -rf $(ALL_ARCH_DIRS)
	rm -f src/*.o
	rm -f $(GLESV2) $(EGLIB) gles_merge.mri
	rm -rf $(PRIV)
	rm -f $(BUILD_DIR)/lib/libportablegl.a $(BUILD_DIR)/lib/libv3dcompiler.a $(BUILD_DIR)/lib/libpglcompiler*.a
	rm -f $(BUILD_DIR)/include/v3dcompiler.h
