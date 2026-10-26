PROJS_ROOT_DIR=../../..
include $(PROJS_ROOT_DIR)/make.inc
.DEFAULT_GOAL := all

TASK = cnn
TASK_DIR = $(SDK_DIR)/rootfs/apps/$(TASK)
TASK_OBJS = $(ARCH)/cnn.$(HW).o
CXXFLAGS += -I$(SDK_DIR)/include -fno-threadsafe-statics
GLES_LIBS = $(SDK_DIR)/lib/libEGL.a $(SDK_DIR)/lib/libGLESv2.a
# Optional imgui support - enable if libimgui_gles.a exists
IMGUI_LIB = $(SDK_DIR)/lib/libimgui_gles.a
ifneq ($(wildcard $(IMGUI_LIB)),)
CXXFLAGS += -DHAS_IMGUI -I$(SDK_DIR)/include/imgui
GLES_LIBS := $(IMGUI_LIB) $(GLES_LIBS)
endif
SYSTEM_LIBS = $(foreach lib,ewoksys ewokstl x++ x font graph widget++ cxx,$(SDK_DIR)/lib/lib$(lib).a)
GPU_HWS := raspi5 raspix
ifeq ($(PGL_G3D),yes)
ifneq ($(filter $(HW),$(GPU_HWS)),)
CXXFLAGS += -DPGL_G3D
endif
endif

PYTHON ?= python3

.PHONY: all shaders clean FORCE $(TASK)
all: $(TASK)

shaders: gen/cnn_glsl.h

FORCE:

gen/cnn_glsl.h: res/common.glsl res/buffer_a.glsl res/buffer_b.glsl res/buffer_c.glsl res/buffer_d.glsl res/image.glsl tools/shader.py FORCE
	$(PYTHON) tools/shader.py

$(TASK_OBJS): cnn.cpp gen/cnn_glsl.h
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) -c $< -o $@

$(TASK): $(TASK_OBJS) $(GLES_LIBS) $(SYSTEM_LIBS)
	@mkdir -p $(TASK_DIR)
	$(LD) -Ttext=100 $(TASK_OBJS) $(GLES_LIBS) -o $(TASK_DIR)/$(TASK) $(LDFLAGS) $(EWOK_LIB_X) $(EWOK_LIB_GRAPH) $(EWOK_LIBC) -lcxx
	cp -r res $(TASK_DIR)

clean:
	rm -rf $(ALL_ARCH_DIRS) gen/*.frag gen/*.h
