PROJS_ROOT_DIR=../../..
include $(PROJS_ROOT_DIR)/make.inc

BUILD_DIR = $(EWOKOS_SYS_DIR)/build_$(ARCH)/$(HW)

# vkmatrix is C++ (Dear ImGui is C++); the effect itself (matrix.c) is plain C.
# <vulkan/vulkan.h> is installed into the shared build include dir by
# libs/vulkan, and the Dear ImGui headers land under include/imgui (installed
# by projects/imgui).
CXXFLAGS += -I $(BUILD_DIR)/include -I $(BUILD_DIR)/include/imgui -D__ewokos__ -fno-threadsafe-statics
CFLAGS += -I $(BUILD_DIR)/include -D__ewokos__
LDFLAGS += -L $(BUILD_DIR)/lib

TASK = vkmatrix
TASK_OBJS = $(ARCH)/vkmatrix.o $(ARCH)/matrix.o
TASK_DIR = $(BUILD_DIR)/rootfs/apps/vulkan/$(TASK)
VULKAN_LIB = $(BUILD_DIR)/lib/libvulkan.a
IMGUI_VK_LIB = $(BUILD_DIR)/lib/libimgui_vk.a

VK_GPU_HWS := raspi5 raspix 8gen3
VK_GPU_LIBS :=
ifeq ($(PGL_G3D),yes)
ifneq ($(filter $(HW),$(VK_GPU_HWS)),)
VK_GPU_LIBS := -lg2dclient -lewokstl
endif
endif

all: $(TASK)

# vkmatrix presents through an EwokOS xwin surface (like vkteapot): the app owns
# the libx window and registers a present callback, so libvulkan.a itself stays
# free of any libx/libgraph dependency.  The Dear ImGui Vulkan overlay
# (libimgui_vk.a) references the runtime's vk* entry points, so it must precede
# libvulkan.a in the link order; ImGui is C++, hence -lcxx on every target.
$(TASK): $(TASK_OBJS) $(IMGUI_VK_LIB) $(VULKAN_LIB) \
         $(SDK_DIR)/lib/libewoksys.a $(SDK_DIR)/lib/libx.a \
         $(SDK_DIR)/lib/libgraph.a $(SDK_DIR)/lib/libfont.a \
         $(SDK_DIR)/lib/libopenlibm.a
	mkdir -p $(TASK_DIR)
	$(LD) -Ttext=100 $(TASK_OBJS) $(IMGUI_VK_LIB) $(VULKAN_LIB) -o $(TASK_DIR)/$(TASK) $(LDFLAGS) $(EWOK_LIB_X) $(EWOK_LIB_GRAPH) -lopenlibm $(VK_GPU_LIBS) $(EWOK_LIBC) -lcxx
	cp -r res $(TASK_DIR)

$(ARCH)/vkmatrix.o: vkmatrix.cpp matrix.h vkmatrix_spirv.h
	@mkdir -p $(ARCH)
	$(CXX) $(CXXFLAGS) -c $< -o $@

$(ARCH)/matrix.o: matrix.c matrix.h
	@mkdir -p $(ARCH)
	$(CC) $(CFLAGS) -c $< -o $@

# Host-side render/benchmark harness (no EwokOS deps): renders raw RGBA frames
# and prints per-frame timings; see tests/host_render.c.
host-test:
	@mkdir -p .host
	cc -O2 -I. tests/host_render.c matrix.c -lm -o .host/host_render

clean:
	rm -rf $(ALL_ARCH_DIRS)
	rm -f *.o
	rm -rf .host

.PHONY: all clean host-test