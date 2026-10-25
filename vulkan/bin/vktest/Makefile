PROJS_ROOT_DIR=../../..
include $(PROJS_ROOT_DIR)/make.inc

BUILD_DIR = $(EWOKOS_SYS_DIR)/build_$(ARCH)/$(HW)

CFLAGS += -I $(BUILD_DIR)/include
LDFLAGS += -L $(BUILD_DIR)/lib

TASK = vktest
TASK_OBJS = $(ARCH)/vktest.o
TASK_DIR = $(BUILD_DIR)/rootfs/bin
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

$(TASK): $(TASK_OBJS) $(VULKAN_LIB) \
         $(SDK_DIR)/lib/libewoksys.a $(SDK_DIR)/lib/libopenlibm.a
	mkdir -p $(TASK_DIR)
	$(LD) -Ttext=100 $(TASK_OBJS) $(VULKAN_LIB) -o $(TASK_DIR)/$(TASK) $(LDFLAGS) -lopenlibm $(VK_GPU_LIBS) $(EWOK_LIBC) $(VK_GPU_CXX)

$(ARCH)/vktest.o: vktest.c vktest_spirv.h
	@mkdir -p $(ARCH)
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -rf $(ALL_ARCH_DIRS)
	rm -f *.o

.PHONY: all clean
