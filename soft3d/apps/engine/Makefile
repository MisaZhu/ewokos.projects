PROJS_ROOT_DIR=../../..
include $(PROJS_ROOT_DIR)/make.inc

TASK_OBJS = $(ARCH)/engine.o $(ARCH)/engine_hud.o

EXTRA_LIBS_DIR = $(EWOKOS_SYS_DIR)/build_$(ARCH)/$(HW)

# <EGL/egl.h> + <GLES2/gl2.h> are installed into the shared build include dir by
# libs/gles, glcommon's headers land there too, and the Dear ImGui headers land
# under include/imgui (installed by libs/imgui).  engine.c is C but the HUD
# wrapper engine_hud.cpp is C++ (ImGui): -I.../include resolves <EGL/...>,
# <GLES2/...> and "imgui/...", and -I.../include/imgui lets the backend header
# find its sibling "imgui.h".
CFLAGS += -I $(EXTRA_LIBS_DIR)/include -I $(EXTRA_LIBS_DIR)/include/glcommon
CXXFLAGS += -I $(EXTRA_LIBS_DIR)/include -I $(EXTRA_LIBS_DIR)/include/glcommon -I $(EXTRA_LIBS_DIR)/include/imgui -D__ewokos__ -fno-threadsafe-statics
LDFLAGS += -L $(EXTRA_LIBS_DIR)/lib

# Link order matters for static archives: the app + imgui reference the EGL /
# GLESv2 entry points.  libGLESv2.a already contains the portablegl engine and
# (on PGL_G3D targets) the on-device mesa GLSL->QPU compiler, so an app links
# libimgui + libEGL + libGLESv2 (imgui -> gl*, EGL -> engine) and never names
# portablegl or pglcompiler.
GLES_LIBS = $(EXTRA_LIBS_DIR)/lib/libglcommon.a \
	$(EXTRA_LIBS_DIR)/lib/libimgui.a \
	$(EXTRA_LIBS_DIR)/lib/libEGL.a \
	$(EXTRA_LIBS_DIR)/lib/libGLESv2.a

TASK = engine
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
