PROJS_ROOT_DIR=../../..
include $(PROJS_ROOT_DIR)/make.inc

BUILD_DIR = $(EWOKOS_SYS_DIR)/build_$(ARCH)/$(HW)

# vkolddisp is C++ (Dear ImGui is C++).  <vulkan/vulkan.h> is installed into the
# shared build include dir by libs/vulkan, and the Dear ImGui headers land
# under include/imgui (installed by projects/imgui).
CXXFLAGS += -I $(BUILD_DIR)/include -I $(BUILD_DIR)/include/imgui -D__ewokos__ -fno-threadsafe-statics
LDFLAGS += -L $(BUILD_DIR)/lib

TASK = vkolddisp
TASK_OBJS = $(ARCH)/vkolddisp.o $(ARCH)/olddisp.o
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

# vkolddisp presents through an EwokOS xwin surface (like vkfonts/vkmatrix): the
# app owns the libx window and registers a present callback, so libvulkan.a
# itself stays free of any libx/libgraph dependency.  The Dear ImGui Vulkan
# overlay (libimgui_vk.a) references the runtime's vk* entry points, so it must
# precede libvulkan.a in the link order; ImGui is C++, hence -lcxx on every
# target.  olddisp.o is the native font engine (Common + Buffer A port); the
# verbatim Buffer B + Image tabs run as the embedded SPIR-V fragment shader.
$(TASK): $(TASK_OBJS) $(IMGUI_VK_LIB) $(VULKAN_LIB) \
         $(SDK_DIR)/lib/libewoksys.a $(SDK_DIR)/lib/libx.a \
         $(SDK_DIR)/lib/libgraph.a $(SDK_DIR)/lib/libfont.a \
         $(SDK_DIR)/lib/libopenlibm.a
	mkdir -p $(TASK_DIR)
	$(LD) -Ttext=100 $(TASK_OBJS) $(IMGUI_VK_LIB) $(VULKAN_LIB) -o $(TASK_DIR)/$(TASK) $(LDFLAGS) $(EWOK_LIB_X) $(EWOK_LIB_GRAPH) -lopenlibm $(VK_GPU_LIBS) $(EWOK_LIBC) -lcxx
	cp -r res $(TASK_DIR)

$(ARCH)/vkolddisp.o: vkolddisp.cpp vkolddisp_spirv.h olddisp.h
	@mkdir -p $(ARCH)
	$(CXX) $(CXXFLAGS) -c $< -o $@

$(ARCH)/olddisp.o: olddisp.c olddisp.h olddisp_font.h
	@mkdir -p $(ARCH)
	$(CC) $(CFLAGS) -c $< -o $@

# Regenerate the embedded SPIR-V after editing shaders/ or res/*.glsl
# (host-only; needs glslangValidator + python3, see shaders/gen_spirv.sh).
spirv:
	./shaders/gen_spirv.sh

# Regenerate the font table after editing res/common.glsl (host-only).
font:
	python3 tools/gen_font.py

clean:
	rm -rf $(ALL_ARCH_DIRS)
	rm -f *.o

.PHONY: all clean spirv font