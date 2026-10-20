PROJS_ROOT_DIR=../../..
include $(PROJS_ROOT_DIR)/make.inc

# ---------------------------------------------------------------------------
# Source layout
#   main.c + shim/*.c          app bootstrap, host imports and EGL/xwin backend
#   upstream/.../gl3/*.c       the vendored yquake2 gl3 refresher (down-ported)
#   upstream/.../files/*.c     shared model/image loaders (md2, pcx, wal, surf)
#   upstream/.../common/shared/*.c  q_shared math/bsp helpers
# The make.rule implicit rule "$(ARCH)/%.o: %.c" matches the nested stems, so an
# object is just $(ARCH)/<relative-source-path>.o and mkdir -p creates the dirs.
# ---------------------------------------------------------------------------
UP          = upstream/src
GL3_DIR     = $(UP)/client/refresh/gl3
FILES_DIR   = $(UP)/client/refresh/files
SHARED_DIR  = $(UP)/common/shared

APP_OBJS = $(ARCH)/main.o \
	$(ARCH)/shim/qshim.o \
	$(ARCH)/shim/vid_egl.o \
	$(ARCH)/shim/ri_impl.o

GL3_OBJS = $(ARCH)/$(GL3_DIR)/gl3_main.o \
	$(ARCH)/$(GL3_DIR)/gl3_draw.o \
	$(ARCH)/$(GL3_DIR)/gl3_image.o \
	$(ARCH)/$(GL3_DIR)/gl3_light.o \
	$(ARCH)/$(GL3_DIR)/gl3_lightmap.o \
	$(ARCH)/$(GL3_DIR)/gl3_mesh.o \
	$(ARCH)/$(GL3_DIR)/gl3_misc.o \
	$(ARCH)/$(GL3_DIR)/gl3_model.o \
	$(ARCH)/$(GL3_DIR)/gl3_shaders.o \
	$(ARCH)/$(GL3_DIR)/gl3_surf.o \
	$(ARCH)/$(GL3_DIR)/gl3_warp.o

FILES_OBJS = $(ARCH)/$(FILES_DIR)/common.o \
	$(ARCH)/$(FILES_DIR)/models.o \
	$(ARCH)/$(FILES_DIR)/pcx.o \
	$(ARCH)/$(FILES_DIR)/pvs.o \
	$(ARCH)/$(FILES_DIR)/stb.o \
	$(ARCH)/$(FILES_DIR)/surf.o \
	$(ARCH)/$(FILES_DIR)/wal.o

SHARED_OBJS = $(ARCH)/$(SHARED_DIR)/shared.o \
	$(ARCH)/$(SHARED_DIR)/rand.o \
	$(ARCH)/$(SHARED_DIR)/flash.o

TASK_OBJS = $(APP_OBJS) $(GL3_OBJS) $(FILES_OBJS) $(SHARED_OBJS)

EXTRA_LIBS_DIR = $(EWOKOS_SYS_DIR)/build_$(ARCH)/$(HW)

# -I $(EXTRA_LIBS_DIR)/include resolves <EGL/...>, <GLES3/gl3.h>, <GLES2/...>,
# <x/xwin.h> and <ewoksys/...>.  The yquake2 -I paths let main.c/shim find
# "ref.h" / "ref_shared.h" and let gl3 find "header/local.h" (which also pulls
# HandmadeMath/model/ewokos_gl_compat from its own dir).  -DYQ2_EWOKOS selects
# the libGLESv2 front-end include path in local.h.  The -Wno-* flags quiet the
# vendored upstream code (unused statics, sign compares) which we don't own.
CFLAGS += -I. -Ishim \
	-I$(UP)/client/refresh \
	-I$(UP)/client/refresh/gl3 \
	-I$(UP)/client/vid/header \
	-I$(UP)/common/header \
	-I $(EXTRA_LIBS_DIR)/include \
	-D__ewokos__ -DYQ2_EWOKOS -DYQ2_GL3_GLES \
	-DYQ2OSTYPE='"EwokOS"' -DYQ2ARCH='"aarch64"' \
	-Wno-unused-variable -Wno-unused-function -Wno-sign-compare \
	-Wno-unused-but-set-variable -Wno-missing-field-initializers
LDFLAGS += -L $(EXTRA_LIBS_DIR)/lib

# Link order matters for static archives: the app + refresher reference the EGL /
# GLESv2 entry points.  libGLESv2.a carries the portablegl engine (and, on
# PGL_G3D targets, the on-device GLSL->QPU compiler), so the app links only
# libEGL + libGLESv2 and never names portablegl or v3dcompiler.
GLES_LIBS = $(EXTRA_LIBS_DIR)/lib/libEGL.a \
	$(EXTRA_LIBS_DIR)/lib/libGLESv2.a

TASK = yquake2
TASK_DIR = $(EWOKOS_SYS_DIR)/build_$(ARCH)/$(HW)/rootfs/apps/$(TASK)

all: $(TASK)

$(TASK): $(TASK_OBJS) $(GLES_LIBS) \
		$(SDK_DIR)/lib/libewoksys.a \
		$(SDK_DIR)/lib/libewokstl.a \
		$(SDK_DIR)/lib/libx++.a \
		$(SDK_DIR)/lib/libx.a \
		$(SDK_DIR)/lib/libfont.a \
		$(SDK_DIR)/lib/libgraph.a \
		$(SDK_DIR)/lib/libwidget++.a \
		$(SDK_DIR)/lib/libcxx.a
	mkdir -p $(TASK_DIR)
	$(LD) -Ttext=100 $(TASK_OBJS) $(GLES_LIBS) -o $(TASK_DIR)/$(TASK) $(LDFLAGS) $(EWOK_LIB_X) $(EWOK_LIB_GRAPH) $(EWOK_LIBC) -lcxx
	cp -r res $(TASK_DIR)

clean:
	rm -rf $(ALL_ARCH_DIRS)
	rm -f *.o
