PROJS_ROOT_DIR=../../..
include $(PROJS_ROOT_DIR)/make.inc

# Pin the default goal to `all` up-front: the explicit $(TASK_OBJS) rule
# below appears before `all:` in the file, and make would otherwise pick
# the first target it sees as the default goal (resulting in `make` just
# reporting "wave.<HW>.o is up to date" without linking).
.DEFAULT_GOAL := all

# HW-suffixed object path: -DPGL_G3D is decided by HW (see the GPU_HWS gate
# below), so a shared $(ARCH)/wave.o would let a raspi5 build leave a
# GPU-shaped object that a subsequent virt build reuses against virt's
# CPU-only libGLESv2.a and fails with:
#   undefined reference to `pglc_compile_set_cs_local_size'
# Encoding HW in the filename makes that collision impossible -- each target
# owns its own .o and switching HW is always a fresh compile.
TASK_OBJS = $(ARCH)/wave.$(HW).o

EXTRA_LIBS_DIR = $(EWOKOS_SYS_DIR)/build_$(ARCH)/$(HW)
# <EGL/egl.h> + <GLES2/gl2.h> are installed into the shared build include dir
# by libs/gles, and the Dear ImGui headers land there under include/imgui
# (installed by libs/imgui).  wave is C++ (ImGui is C++): -I.. reaches the
# app's own headers (wave_glsl.h), -I.../include resolves <EGL/...>,
# <GLES2/...> and "imgui/...", and -I.../include/imgui lets the backend
# header find its sibling "imgui.h".
CFLAGS += -I.. -I $(EXTRA_LIBS_DIR)/include -I $(EXTRA_LIBS_DIR)/include/imgui -D__ewokos__
CXXFLAGS += -I.. -I $(EXTRA_LIBS_DIR)/include -I $(EXTRA_LIBS_DIR)/include/imgui -D__ewokos__ -fno-threadsafe-statics
LDFLAGS += -L $(EXTRA_LIBS_DIR)/lib

# The app Makefile does NOT define -DPGL_G3D for the app translation unit even
# when PGL_G3D=yes is passed on the make line; the app has an #ifdef PGL_G3D
# block (pglc_compile_set_cs_local_size argv knob), so add it explicitly here
# or the GPU branch is silently compiled out.
#
# Guard on BOTH PGL_G3D=yes AND a GPU-capable HW: virt is CPU-only and its
# merged libGLESv2.a omits pglcompiler_frontend.o, so activating the app's
# #ifdef PGL_G3D knob there would leave pglc_compile_set_cs_local_size
# unresolved at link time.  Filtering by HW also makes the aggregate build
# safe if PGL_G3D=yes leaks in from a machine-level make.inc for a
# non-GPU target.
GPU_HWS := raspi5 raspix
ifeq ($(PGL_G3D),yes)
ifneq ($(filter $(HW),$(GPU_HWS)),)
CFLAGS   += -DPGL_G3D
CXXFLAGS += -DPGL_G3D
endif
endif

# Build-time shader lint for GPU targets (the "must enter GPU" guard).
# The on-device mesa GLSL->QPU compiler on raspi5/raspix is a CONFORMING GLSL ES
# compiler: it rejects user identifiers that use the reserved gl_ prefix at link
# time, which silently drops the program to the CPU interpreter (HUD reads
# "Renderer: CPU") even though the platform supports GPU -- the CPU interpreter
# is lenient and accepts them, so the bug only shows on-device.  Catch that class
# of bug HERE so a GPU build can never ship a shader that would never enter the
# GPU.  Non-GPU HWs skip the check (no mesa, no GPU route).
.PHONY: shader-lint
ifeq ($(filter $(HW),$(GPU_HWS)),)
shader-lint:
	@:
else
shader-lint:
	@bad=$$(grep -ohE '\bgl_[A-Za-z0-9_]*' wave_glsl.h | sort -u \
		| grep -vE '^(gl_FragColor|gl_FragCoord|gl_Position|gl_PointCoord|gl_PointSize|gl_FragData)$$'); \
	if [ -n "$$bad" ]; then \
		echo "shader-lint FAIL: reserved gl_ identifier(s) in wave_glsl.h would be"; \
		echo "  rejected by the on-device mesa compiler and silently force a CPU"; \
		echo "  fallback on $(HW) even though the platform supports GPU:"; \
		echo "$$bad" | sed 's/^/    /'; \
		exit 1; \
	fi
endif

# Explicit rule (overrides make.rule's pattern rule, whose stem would look
# for a source named wave.$(HW).cpp).  -MMD -MP are already in CXXFLAGS via
# make.rule, so the .d file lands at $(ARCH)/wave.$(HW).d and gets picked up
# by make.rule's DEP_FILES include.
$(TASK_OBJS): wave.cpp wave_glsl.h
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) -c $< -o $@

# Link order matters for static archives: the app + imgui reference the EGL /
# GLESv2 entry points.  libGLESv2.a already contains the portablegl engine
# and (on PGL_G3D targets) the on-device mesa GLSL->QPU compiler, so an app
# links only libimgui + libEGL + libGLESv2 (imgui -> gl*, EGL -> engine) and
# never names portablegl or pglcompiler.
GLES_LIBS = $(EXTRA_LIBS_DIR)/lib/libimgui.a \
	$(EXTRA_LIBS_DIR)/lib/libEGL.a \
	$(EXTRA_LIBS_DIR)/lib/libGLESv2.a

TASK = wave
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
	@# The baked faithful loop ships on EVERY target: virt plays it as its only
	@# route, and GPU targets keep it as the faithful decline fallback (the
	@# cheap variant no longer exists), so a GPU refusal still shows the
	@# identical faithful pixels instead of any simplified look.

clean:
	rm -rf $(ALL_ARCH_DIRS)
	rm -f *.o
