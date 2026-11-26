PROJS_ROOT_DIR=../../..
include $(PROJS_ROOT_DIR)/make.inc

BUILD_DIR = $(EWOKOS_SYS_DIR)/build_$(ARCH)/$(HW)

# vkfonts is C++ (Dear ImGui is C++).  <vulkan/vulkan.h> is installed into the
# shared build include dir by libs/vulkan, and the Dear ImGui headers land
# under include/imgui (installed by projects/imgui).
CXXFLAGS += -I $(BUILD_DIR)/include -I $(BUILD_DIR)/include/imgui -D__ewokos__ -fno-threadsafe-statics
LDFLAGS += -L $(BUILD_DIR)/lib

TASK = vkfonts
TASK_OBJS = $(ARCH)/vkfonts.o
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

# vkfonts presents through an EwokOS xwin surface (like vkteapot/vkmatrix): the
# app owns the libx window and registers a present callback, so libvulkan.a
# itself stays free of any libx/libgraph dependency.  The Dear ImGui Vulkan
# overlay (libimgui_vk.a) references the runtime's vk* entry points, so it must
# precede libvulkan.a in the link order; ImGui is C++, hence -lcxx on every
# target.  On the VK_GPU_HWS machines with PGL_G3D=yes the FS/VS pair now fits
# the GPU transpiler subset (helper calls, local arrays, inout params), so the
# pipeline runs on the Adreno/V3D geom kernels; elsewhere it stays on the CPU
# interpreter.  res/font.png is decoded with libgraph's png_image_new (-lpng
# -lz already ride EWOK_LIB_GRAPH).
$(TASK): $(TASK_OBJS) $(IMGUI_VK_LIB) $(VULKAN_LIB) \
         $(SDK_DIR)/lib/libewoksys.a $(SDK_DIR)/lib/libx.a \
         $(SDK_DIR)/lib/libgraph.a $(SDK_DIR)/lib/libfont.a \
         $(SDK_DIR)/lib/libopenlibm.a
	mkdir -p $(TASK_DIR)
	$(LD) -Ttext=100 $(TASK_OBJS) $(IMGUI_VK_LIB) $(VULKAN_LIB) -o $(TASK_DIR)/$(TASK) $(LDFLAGS) $(EWOK_LIB_X) $(EWOK_LIB_GRAPH) -lopenlibm $(VK_GPU_LIBS) $(EWOK_LIBC) -lcxx
	cp -r res $(TASK_DIR)

$(ARCH)/vkfonts.o: vkfonts.cpp vkfonts_spirv.h
	@mkdir -p $(ARCH)
	$(CXX) $(CXXFLAGS) -c $< -o $@

# Regenerate the embedded SPIR-V after editing shaders/ or res/*.glsl
# (host-only; needs glslangValidator + python3, see shaders/gen_spirv.sh).
spirv:
	./shaders/gen_spirv.sh

clean:
	rm -rf $(ALL_ARCH_DIRS)
	rm -f *.o

.PHONY: all clean spirv