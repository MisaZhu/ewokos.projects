PROJS_ROOT_DIR=../../..
include $(PROJS_ROOT_DIR)/make.inc

BUILD_DIR = $(EWOKOS_SYS_DIR)/build_$(ARCH)/$(HW)

# vkteapot is now C++ (Dear ImGui is C++).  <vulkan/vulkan.h> is installed into
# the shared build include dir by libs/vulkan, and the Dear ImGui headers land
# under include/imgui (installed by projects/imgui).  -I $(BUILD_DIR)/include
# resolves <vulkan/...> and "imgui/...", and -I $(BUILD_DIR)/include/imgui lets
# imgui_impl_vulkan.h find its sibling "imgui.h".
CXXFLAGS += -I $(BUILD_DIR)/include -I $(BUILD_DIR)/include/imgui -D__ewokos__ -fno-threadsafe-statics
LDFLAGS += -L $(BUILD_DIR)/lib

TASK = vkteapot
TASK_OBJS = $(ARCH)/vkteapot.o
TASK_DIR = $(BUILD_DIR)/rootfs/apps/$(TASK)
VULKAN_LIB = $(BUILD_DIR)/lib/libvulkan.a
IMGUI_VK_LIB = $(BUILD_DIR)/lib/libimgui_vk.a

VK_GPU_HWS := raspi5 q8b
VK_GPU_LIBS :=
ifeq ($(PGL_G3D),yes)
ifneq ($(filter $(HW),$(VK_GPU_HWS)),)
VK_GPU_LIBS := -lg2dclient -lewokstl
endif
endif

all: $(TASK)

# vkteapot presents through an EwokOS xwin surface (like bin/triangle): the app
# owns the libx window and registers a present callback, so libvulkan.a itself
# stays free of any libx/libgraph dependency.  The app links the windowing libs,
# not the runtime.  The Dear ImGui Vulkan overlay (libimgui_vk.a) references the
# runtime's vk* entry points, so it must precede libvulkan.a in the link order;
# ImGui is C++, hence -lcxx on every target.  On the GPU targets (PGL_G3D:
# raspi5, q8b) the teapot draw qualifies for the GPU back-end and the
# g2dclient/ewokstl libs are pulled in; elsewhere it is CPU-only.  On q8b the
# same geom_kind-5 pipeline is compiled to Adreno compute kernels and dispatched
# through the machine's PGL backend.
$(TASK): $(TASK_OBJS) $(IMGUI_VK_LIB) $(VULKAN_LIB) \
         $(SDK_DIR)/lib/libewoksys.a $(SDK_DIR)/lib/libx.a \
         $(SDK_DIR)/lib/libgraph.a $(SDK_DIR)/lib/libfont.a \
         $(SDK_DIR)/lib/libopenlibm.a
	mkdir -p $(TASK_DIR)
	$(LD) -Ttext=100 $(TASK_OBJS) $(IMGUI_VK_LIB) $(VULKAN_LIB) -o $(TASK_DIR)/$(TASK) $(LDFLAGS) $(EWOK_LIB_X) $(EWOK_LIB_GRAPH) -lopenlibm $(VK_GPU_LIBS) $(EWOK_LIBC) -lcxx
	cp -r res $(TASK_DIR)

$(ARCH)/vkteapot.o: vkteapot.cpp teapot_data.h vkteapot_spirv.h
	@mkdir -p $(ARCH)
	$(CXX) $(CXXFLAGS) -c $< -o $@

clean:
	rm -rf $(ALL_ARCH_DIRS)
	rm -f *.o

.PHONY: all clean
