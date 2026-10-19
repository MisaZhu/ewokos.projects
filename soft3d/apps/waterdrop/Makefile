PROJS_ROOT_DIR=../../..
include $(PROJS_ROOT_DIR)/make.inc

TASK_OBJS = $(ARCH)/waterdrop.o

EXTRA_LIBS_DIR = $(EWOKOS_SYS_DIR)/build_$(ARCH)/$(HW)

# <EGL/egl.h>, <GLES2/gl2.h> and "glcommon/hud.h" are all installed into the
# shared build include dir (by libs/gles and libs/glcommon), so a single
# -I.../include resolves every external header the app uses; -I.. reaches the
# apps/ tree for the app's own headers.
CFLAGS += -I.. -I $(EXTRA_LIBS_DIR)/include -D__ewokos__
CXXFLAGS += -I.. -I $(EXTRA_LIBS_DIR)/include -D__ewokos__ -fno-threadsafe-statics
LDFLAGS += -L $(EXTRA_LIBS_DIR)/lib

# Link order matters for static archives: libglcommon (the HUD) and libEGL both
# resolve their gl*/egl* calls against libGLESv2, so GLESv2 comes last (the same
# glcommon -> EGL -> GLESv2 order the demos and apps/gears use).  libGLESv2.a
# already contains the portablegl engine and (on PGL_G3D targets) the on-device
# mesa GLSL->QPU compiler, so the app never names portablegl or v3dcompiler.
GLES_LIBS = $(EXTRA_LIBS_DIR)/lib/libglcommon.a \
	$(EXTRA_LIBS_DIR)/lib/libEGL.a \
	$(EXTRA_LIBS_DIR)/lib/libGLESv2.a

TASK = waterdrop
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
