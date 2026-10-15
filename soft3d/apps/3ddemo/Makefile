PROJS_ROOT_DIR=../../..
include $(PROJS_ROOT_DIR)/make.inc

TASK_OBJS = $(ARCH)/3ddemo.o

EXTRA_LIBS_DIR = $(EWOKOS_SYS_DIR)/build_$(ARCH)/$(HW)

CXXFLAGS += -I $(EXTRA_LIBS_DIR)/include -I $(EXTRA_LIBS_DIR)/include/glcommon
LDFLAGS += -L $(EXTRA_LIBS_DIR)/lib

# PGL_G3D (raspi5): the portablegl GPU backend pulls in pgl_g3d_shader.o, which
# references the on-device mesa compiler (v3d_compile_glsl) in libv3dcompiler.a;
# group it with libc/libm/cxx so its refs resolve (mirrors libs/demos/Makefile).
# -DPGL_G3D also reaches 3ddemo.cpp so its #ifdef PGL_G3D full-GPU compiled-mesh
# path (VS+FS on the QPU via pgl_g3d_upload_mesh / pgl_g3d_draw_mesh_compiled +
# a tiled sampler texture) is compiled in; without it 3ddemo.cpp builds the CPU
# basic_transform_vp + pgl_ff_tex_modulate path only.
ifeq ($(PGL_G3D),yes)
CXXFLAGS += -DPGL_G3D
LDFLAGS += --start-group -lv3dcompiler $(EWOK_LIBC) -lcxx -lopenlibm -lm --end-group
endif

GLCOMMON_LIB = $(EXTRA_LIBS_DIR)/lib/libglcommon.a \
	$(EXTRA_LIBS_DIR)/lib/libportablegl.a

TASK = 3ddemo
TASK_DIR = $(EWOKOS_SYS_DIR)/build_$(ARCH)/$(HW)/rootfs/apps/$(TASK)

all: $(TASK)

$(TASK): $(TASK_OBJS) $(GLCOMMON_LIB) \
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
	$(LD) -Ttext=100 $(TASK_OBJS) $(GLCOMMON_LIB) -o $(TASK_DIR)/$(TASK) $(LDFLAGS) $(EWOK_LIB_X) $(EWOK_LIB_GRAPH) -lopenlibm $(EWOK_LIBC) -lcxx
	cp -r res $(TASK_DIR)

clean:
	rm -rf $(ALL_ARCH_DIRS)
	rm -f *.o
