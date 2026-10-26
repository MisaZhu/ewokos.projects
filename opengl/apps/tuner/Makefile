PROJS_ROOT_DIR=../../..
include $(PROJS_ROOT_DIR)/make.inc
.DEFAULT_GOAL := all

TASK = tuner
TASK_DIR = $(SDK_DIR)/rootfs/apps/$(TASK)
TASK_OUT = $(TASK_DIR)/$(TASK)
TUNER_VALIDATE ?= 0
# Isolate objects across the GPU and CPU SDKs.
TASK_OBJS = $(ARCH)/tuner.$(HW).o
ifeq ($(TUNER_VALIDATE),1)
TASK_OBJS = $(ARCH)/tuner.$(HW).validate.o
TASK_OUT = .build/$(ARCH)/$(HW)/tuner-validate
CXXFLAGS += -DTUNER_VALIDATE
endif
# The app Makefile does NOT define -DPGL_G3D for the app translation unit even
# when PGL_G3D=yes is passed on the make line; tuner has an #ifdef PGL_G3D block
# (the pgl_g3d_set_render_scale resolution argv), so add it explicitly here or
# the GPU branch is silently compiled out.  Guard on BOTH PGL_G3D=yes AND a
# GPU-capable HW: virt is CPU-only and its merged libGLESv2.a omits the GPU
# engine, so activating the knob there would leave pgl_g3d_set_render_scale
# unresolved at link time.
GPU_HWS := raspi5 raspix
ifeq ($(PGL_G3D),yes)
ifneq ($(filter $(HW),$(GPU_HWS)),)
CXXFLAGS += -DPGL_G3D
endif
endif
CXXFLAGS += -I$(SDK_DIR)/include -I$(SDK_DIR)/include/imgui -D__ewokos__ -fno-threadsafe-statics
GLES_LIBS = $(SDK_DIR)/lib/libimgui_gles.a $(SDK_DIR)/lib/libEGL.a $(SDK_DIR)/lib/libGLESv2.a
SYSTEM_LIBS = $(foreach lib,ewoksys ewokstl x++ x font graph widget++ cxx,$(SDK_DIR)/lib/lib$(lib).a)
PYTHON ?= python3
HOST_CC ?= cc
HOST_CFLAGS ?= -std=c11 -O1 -ffp-contract=off
HOST_OUT ?= .host/shader-test
GLSL_DIR = ../../libs/gles/src/glsl
GLSL_SOURCES = $(addprefix $(GLSL_DIR)/,glsl_lex.c glsl_parse.c glsl_program.c glsl_exec.c)
GLSL_HEADERS = $(wildcard $(GLSL_DIR)/*.h $(GLSL_DIR)/*.inc)

.PHONY: all shaders test test-full test-compare clean $(TASK)
all: $(TASK)

# Also verifies the SHA-256 of the preserved reference body. No network fetches.
shaders:
	HOST_CC="$(HOST_CC)" $(PYTHON) tools/shader.py

gen/tuner_glsl.h: res/image.glsl tools/shader.py
	HOST_CC="$(HOST_CC)" $(PYTHON) tools/shader.py

$(TASK_OBJS): tuner.cpp gen/tuner_glsl.h Makefile
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) -c $< -o $@

$(TASK): $(TASK_OBJS) $(GLES_LIBS) $(SYSTEM_LIBS)
	@mkdir -p $(dir $(TASK_OUT))
	$(LD) -Ttext=100 $(TASK_OBJS) $(GLES_LIBS) -o $(TASK_OUT) $(LDFLAGS) $(EWOK_LIB_X) $(EWOK_LIB_GRAPH) $(EWOK_LIBC) -lcxx
ifneq ($(TUNER_VALIDATE),1)
	cp -r res $(TASK_DIR)
endif

# REFERENCE_DIR optionally names captured ref-{0,1,4,8,16,32}.rgba frames.
test: $(HOST_OUT)
	$(HOST_OUT) $(if $(REFERENCE_DIR),"$(REFERENCE_DIR)")

# Render every pixel at t=0 through the CPU interpreter (no quality reduction).
test-full: $(HOST_OUT)
	$(HOST_OUT) $(if $(REFERENCE_DIR),"$(REFERENCE_DIR)") --full

test-compare: $(HOST_OUT)
	$(HOST_OUT) $(if $(REFERENCE_DIR),"$(REFERENCE_DIR)") --compare

$(HOST_OUT): tests/shader_test.c gen/tuner_glsl.h $(GLSL_SOURCES) $(GLSL_HEADERS)
	@mkdir -p $(dir $@)
	$(HOST_CC) $(HOST_CFLAGS) tests/shader_test.c $(GLSL_SOURCES) -lm -o $@

clean:
	rm -rf $(ALL_ARCH_DIRS) .host .build
