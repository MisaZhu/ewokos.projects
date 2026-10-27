PROJS_ROOT_DIR=../../..
include $(PROJS_ROOT_DIR)/make.inc
.DEFAULT_GOAL := all

TASK = macgl
TASK_DIR = $(SDK_DIR)/rootfs/apps/$(TASK)
TASK_OBJS = $(ARCH)/macgl.$(HW).o
CXXFLAGS += -I$(SDK_DIR)/include -I$(SDK_DIR)/include/imgui -fno-threadsafe-statics
GLES_LIBS = $(SDK_DIR)/lib/libimgui_gles.a $(SDK_DIR)/lib/libEGL.a $(SDK_DIR)/lib/libGLESv2.a
SYSTEM_LIBS = $(foreach lib,ewoksys ewokstl x++ x font graph widget++ cxx,$(SDK_DIR)/lib/lib$(lib).a)
# q8b exports PGL_G3D=yes too (machines/q8b/system/make.inc: Adreno a6xx
# backend), so its libGLESv2 auto-detects macgl's passthrough-VS +
# gl_FragCoord-FS quad as a fullscreen effect and GPU-compiles it (see
# pgl_engine.c is_fullscreen).  If the APP is built WITHOUT -DPGL_G3D it hands
# the engine macgl_cpu_fs_glsl -- the interpreter-only shader with large
# dynamically-indexed const tables -- and never supplies the macgl_gpu_fs_glsl
# override (glSetGpuShaderSourceEWOK) nor uploads the macglData 4x512 lookup
# atlas, so Adreno runs the wrong shader with no data texture: the display is
# corrupt.  Gating q8b here makes it build the designed GPU path exactly like
# raspi5 (interpreter macgl_fs_glsl + GPU macgl_gpu_fs_glsl + macglData atlas);
# the q8b fullscreen-compute kernel stages that REPEAT/linear RGBA8 texture and
# declines to the (correct) interpreter if it ever cannot.
GPU_HWS := raspi5 raspix q8b
ifeq ($(PGL_G3D),yes)
ifneq ($(filter $(HW),$(GPU_HWS)),)
CXXFLAGS += -DPGL_G3D
endif
endif

PYTHON ?= python3
HOST_CC ?= cc
HOST_CFLAGS ?= -std=c11 -O2 -ffp-contract=off
HOST_OUT ?= .host/shader-test
GLSL_DIR = ../../libs/gles/src/glsl
GLSL_SOURCES = $(addprefix $(GLSL_DIR)/,glsl_lex.c glsl_parse.c glsl_program.c glsl_exec.c)
GLSL_HEADERS = $(wildcard $(GLSL_DIR)/*.h $(GLSL_DIR)/*.inc)

.PHONY: all shaders test clean FORCE $(TASK)
all: $(TASK)

# A single generation edge is shared by parallel app and test builds. The
# generator verifies both hashes but leaves unchanged output timestamps intact.
shaders: gen/macgl_glsl.h

FORCE:

gen/macgl_glsl.h: res/common.glsl res/image.glsl tools/shader.py FORCE
	$(PYTHON) tools/shader.py

$(TASK_OBJS): macgl.cpp gen/macgl_glsl.h
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) -c $< -o $@

$(TASK): $(TASK_OBJS) $(GLES_LIBS) $(SYSTEM_LIBS)
	@mkdir -p $(TASK_DIR)
	$(LD) -Ttext=100 $(TASK_OBJS) $(GLES_LIBS) -o $(TASK_DIR)/$(TASK) $(LDFLAGS) $(EWOK_LIB_X) $(EWOK_LIB_GRAPH) $(EWOK_LIBC) -lcxx
	cp -r res $(TASK_DIR)

# Captures are bottom-up 320x180 RGBA8 from the original WebGL2 shader.
test: shaders $(HOST_OUT)
	$(HOST_OUT) $(if $(REFERENCE_DIR),"$(REFERENCE_DIR)") $(TEST_ARGS)

$(HOST_OUT): tests/shader_test.c gen/macgl_glsl.h $(GLSL_SOURCES) $(GLSL_HEADERS)
	@mkdir -p $(dir $@)
	$(HOST_CC) $(HOST_CFLAGS) tests/shader_test.c $(GLSL_SOURCES) -lm -o $@

clean:
	rm -rf $(ALL_ARCH_DIRS) .host
