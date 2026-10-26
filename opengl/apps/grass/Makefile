PROJS_ROOT_DIR=../../..
include $(PROJS_ROOT_DIR)/make.inc

# Pin the default goal to `all` up-front: the explicit $(TASK_OBJS) rule below
# appears before `all:` in the file, and make would otherwise pick the first
# target it sees as the default goal.
.DEFAULT_GOAL := all

# HW-suffixed object path: -DPGL_G3D is decided by HW (see the GPU_HWS gate
# below), so a shared $(ARCH)/grass.o would let a raspi5 build leave a
# GPU-shaped object that a subsequent virt build reuses against virt's CPU-only
# libGLESv2.a and fails.  Encoding HW in the filename makes that impossible.
TASK_OBJS = $(ARCH)/grass.$(HW).o

EXTRA_LIBS_DIR = $(EWOKOS_SYS_DIR)/build_$(ARCH)/$(HW)

# <EGL/egl.h> + <GLES2/gl2.h> are installed into the shared build include dir by
# libs/gles; the Dear ImGui headers land under include/imgui (projects/imgui); and
# glcommon installs gltools.h under include/glcommon with its bundled
# stb_image.h alongside (libs/glcommon).  grass is C++ (ImGui + std::string):
# -I.. reaches the app's own dir, -I.../include resolves <EGL/...>, <GLES2/...>,
# "imgui/..." and "glcommon/gltools.h", -I.../include/imgui lets the backend
# header find its sibling "imgui.h", and -I.../include/glcommon lets gltools.h
# find <stb_image.h>.
CFLAGS += -I.. -I $(EXTRA_LIBS_DIR)/include -I $(EXTRA_LIBS_DIR)/include/imgui -I $(EXTRA_LIBS_DIR)/include/glcommon -D__ewokos__
CXXFLAGS += -I.. -I $(EXTRA_LIBS_DIR)/include -I $(EXTRA_LIBS_DIR)/include/imgui -I $(EXTRA_LIBS_DIR)/include/glcommon -D__ewokos__ -fno-threadsafe-statics
LDFLAGS += -L $(EXTRA_LIBS_DIR)/lib

# The app Makefile does NOT define -DPGL_G3D for the app translation unit even
# when PGL_G3D=yes is passed on the make line; the app has #ifdef PGL_G3D blocks
# (the pglc_compile_set_cs_local_size workgroup-shape argv and the
# pgl_g3d_set_render_scale resolution argv), so add it explicitly here.  Guard on
# BOTH PGL_G3D=yes AND a GPU-capable HW: virt is CPU-only and its merged
# libGLESv2.a omits pglcompiler_frontend.o, so activating the app's
# #ifdef PGL_G3D knob there would leave pglc_compile_set_cs_local_size
# unresolved at link time.
GPU_HWS := raspi5 raspix
ifeq ($(PGL_G3D),yes)
ifneq ($(filter $(HW),$(GPU_HWS)),)
CFLAGS   += -DPGL_G3D
CXXFLAGS += -DPGL_G3D
endif
endif

# Build-time shader lint for GPU targets (the "must enter GPU" guard).  The
# on-device mesa GLSL->QPU compiler on raspi5/raspix is a CONFORMING GLSL ES
# compiler: it rejects user identifiers that use the reserved built-in prefix at
# link time, which silently drops the program to the CPU interpreter even though
# the platform supports GPU.  Catch that class of bug HERE.  Non-GPU HWs skip the
# check (no mesa, no GPU route).
.PHONY: shader-lint
ifeq ($(filter $(HW),$(GPU_HWS)),)
shader-lint:
	@:
else
shader-lint:
	@bad=$$(grep -ohE '\bgl_[A-Za-z0-9_]*' grass_glsl.h | sort -u \
		| grep -vE '^(gl_FragColor|gl_FragCoord|gl_Position|gl_PointCoord|gl_PointSize|gl_FragData)$$'); \
	if [ -n "$$bad" ]; then \
		echo "shader-lint FAIL: reserved gl_ identifier(s) in grass_glsl.h would be"; \
		echo "  rejected by the on-device mesa compiler and silently force a CPU"; \
		echo "  fallback on $(HW) even though the platform supports GPU:"; \
		echo "$$bad" | sed 's/^/    /'; \
		exit 1; \
	fi
endif

# Explicit rule (overrides make.rule's pattern rule, whose stem would look for a
# source named grass.$(HW).cpp).  -MMD -MP are already in CXXFLAGS via make.rule,
# so the .d file lands at $(ARCH)/grass.$(HW).d and gets picked up by make.rule's
# DEP_FILES include.
$(TASK_OBJS): grass.cpp grass_glsl.h
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) -c $< -o $@

# Link order matters for static archives: grass.o references ImGui, glcommon's
# load_texture2D and the EGL / GLESv2 entry points; libglcommon and libimgui both
# resolve their gl* calls against libEGL -> libGLESv2, so libGLESv2 comes last
# (the same glcommon -> EGL -> GLESv2 order libs/demos uses).  libGLESv2.a
# already contains the portablegl engine and (on PGL_G3D targets) the on-device
# mesa GLSL->QPU compiler, so the app never names portablegl or pglcompiler.
GLES_LIBS = $(EXTRA_LIBS_DIR)/lib/libimgui_gles.a \
        $(EXTRA_LIBS_DIR)/lib/libglcommon.a \
        $(EXTRA_LIBS_DIR)/lib/libEGL.a \
        $(EXTRA_LIBS_DIR)/lib/libGLESv2.a

TASK = grass
TASK_DIR = $(EWOKOS_SYS_DIR)/build_$(ARCH)/$(HW)/rootfs/apps/$(TASK)

all: shader-lint $(TASK)

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
