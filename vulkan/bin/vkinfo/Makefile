PROJS_ROOT_DIR=../../..
include $(PROJS_ROOT_DIR)/make.inc

BUILD_DIR = $(EWOKOS_SYS_DIR)/build_$(ARCH)/$(HW)

CFLAGS += -I $(BUILD_DIR)/include
LDFLAGS += -L $(BUILD_DIR)/lib

TASK = vkinfo
TASK_OBJS = $(ARCH)/$(TASK).o
TASK_DIR = $(BUILD_DIR)/rootfs/bin
VULKAN_LIB = $(BUILD_DIR)/lib/libvulkan.a

# GPU targets (raspi5/raspix V3D CSD, q8b Adreno 690 CS): libvulkan.a additionally
# folds in vkclient + the on-device mesa compiler, so the command must also link
# the g2d transport and the C++ runtime mesa needs.  Gated on the same
# PGL_G3D=yes + VK-GPU-HW condition libs/vulkan uses, so a CPU-only target keeps
# its exact link line.
VK_GPU_HWS := raspi5 raspix q8b
VK_GPU_LIBS :=
VK_GPU_CXX :=
ifeq ($(PGL_G3D),yes)
ifneq ($(filter $(HW),$(VK_GPU_HWS)),)
VK_GPU_LIBS := -lg2dclient -lewokstl
VK_GPU_CXX := -lcxx
endif
endif

all: $(TASK)

$(TASK): $(TASK_OBJS) $(VULKAN_LIB) \
         $(SDK_DIR)/lib/libewoksys.a $(SDK_DIR)/lib/libopenlibm.a
	mkdir -p $(TASK_DIR)
	$(LD) -Ttext=100 $(TASK_OBJS) $(VULKAN_LIB) -o $(TASK_DIR)/$(TASK) $(LDFLAGS) -lopenlibm $(VK_GPU_LIBS) $(EWOK_LIBC) $(VK_GPU_CXX)

$(ARCH)/vkinfo.o: vkinfo.c
	@mkdir -p $(ARCH)
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -rf $(ALL_ARCH_DIRS)
	rm -f *.o

.PHONY: all clean
