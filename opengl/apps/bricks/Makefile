PROJS_ROOT_DIR=../../..
include $(PROJS_ROOT_DIR)/make.inc

.DEFAULT_GOAL := all

TASK_OBJS = $(ARCH)/bricks.o

$(ARCH)/bricks.o: bricks_gpu_glsl.h

EXTRA_LIBS_DIR = $(EWOKOS_SYS_DIR)/build_$(ARCH)/$(HW)

# <EGL/egl.h> + <GLES2/gl2.h> + <GLES2/gl2ext.h> are installed into the shared
# build include dir by libs/gles. bricks embeds its CPU blit shaders and the GPU
# image pass, draws no ImGui HUD, and loads no images, so -I.../include resolves
# every GL header. Compile flags are identical on CPU and GPU machines: routing
# is decided by the platform at program link/draw, with the existing CPU image
# pass retained as fallback, so one $(ARCH)/bricks.o is correct.
CXXFLAGS += -I $(EXTRA_LIBS_DIR)/include -D__ewokos__ -fno-threadsafe-statics
LDFLAGS += -L $(EXTRA_LIBS_DIR)/lib

# Link order matters for static archives: bricks references the EGL / GLESv2
# entry points.  libGLESv2.a already contains the portablegl engine (and, on
# PGL_G3D targets, the on-device mesa GLSL->QPU compiler), so the app links the
# standard trio libglcommon + libEGL + libGLESv2 and never names portablegl or
# pglcompiler.  libimgui_gles.a is intentionally omitted (no HUD).
GLES_LIBS = $(EXTRA_LIBS_DIR)/lib/libglcommon.a \
	$(EXTRA_LIBS_DIR)/lib/libEGL.a \
	$(EXTRA_LIBS_DIR)/lib/libGLESv2.a

TASK = bricks
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
		$(SDK_DIR)/lib/libopenlibm.a \
		$(SDK_DIR)/lib/libcxx.a
	mkdir -p $(TASK_DIR)
	$(LD) -Ttext=100 $(TASK_OBJS) $(GLES_LIBS) -o $(TASK_DIR)/$(TASK) $(LDFLAGS) $(EWOK_LIB_X) $(EWOK_LIB_GRAPH) -lopenlibm $(EWOK_LIBC) -lcxx
	cp -r res $(TASK_DIR)

clean:
	rm -rf $(ALL_ARCH_DIRS)
	rm -f *.o
