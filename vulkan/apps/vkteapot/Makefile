PROJS_ROOT_DIR=../../..
include $(PROJS_ROOT_DIR)/make.inc

BUILD_DIR = $(EWOKOS_SYS_DIR)/build_$(ARCH)/$(HW)

CFLAGS += -I $(BUILD_DIR)/include
LDFLAGS += -L $(BUILD_DIR)/lib

TASK = vkteapot
TASK_OBJS = $(ARCH)/vkteapot.o
TASK_DIR = $(BUILD_DIR)/rootfs/apps/$(TASK)
VULKAN_LIB = $(BUILD_DIR)/lib/libvulkan.a

VK_GPU_HWS := raspi5
VK_GPU_LIBS :=
VK_GPU_CXX :=
ifeq ($(PGL_G3D),yes)
ifneq ($(filter $(HW),$(VK_GPU_HWS)),)
VK_GPU_LIBS := -lg2dclient -lewokstl
VK_GPU_CXX := -lcxx
endif
endif

all: $(TASK)

# vkteapot presents through an EwokOS xwin surface (like bin/triangle): the app
# owns the libx window and registers a present callback, so libvulkan.a itself
# stays free of any libx/libgraph dependency.  The app links the windowing libs,
# not the runtime.  On raspi5 (PGL_G3D) the draw qualifies for the GPU back-end
# and the g2dclient/ewokstl/cxx libs are pulled in; elsewhere it is CPU-only.
$(TASK): $(TASK_OBJS) $(VULKAN_LIB) \
         $(SDK_DIR)/lib/libewoksys.a $(SDK_DIR)/lib/libx.a \
         $(SDK_DIR)/lib/libgraph.a $(SDK_DIR)/lib/libfont.a \
         $(SDK_DIR)/lib/libopenlibm.a
	mkdir -p $(TASK_DIR)
	$(LD) -Ttext=100 $(TASK_OBJS) $(VULKAN_LIB) -o $(TASK_DIR)/$(TASK) $(LDFLAGS) $(EWOK_LIB_X) $(EWOK_LIB_GRAPH) -lopenlibm $(VK_GPU_LIBS) $(EWOK_LIBC) $(VK_GPU_CXX)
	cp -r res $(TASK_DIR)

$(ARCH)/vkteapot.o: vkteapot.c teapot_data.h vkteapot_spirv.h
	@mkdir -p $(ARCH)
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -rf $(ALL_ARCH_DIRS)
	rm -f *.o

.PHONY: all clean
