PROJS_ROOT_DIR=../../..
include $(PROJS_ROOT_DIR)/make.inc

BUILD_DIR = $(EWOKOS_SYS_DIR)/build_$(ARCH)/$(HW)

# Object dir keyed by $(HW) as well as $(ARCH) (mirrors libs/vulkan + libs/gles):
# the GPU gate below preprocesses differently per machine, so each machine gets
# its own objects and switching HW is always a fresh compile.
OBJDIR = $(ARCH)/$(HW)

# vkclient is a PRIVATE intermediate: libvkclient.a is folded into libvulkan.a by
# libs/vulkan/Makefile (an ar ADDLIB merge), never installed into $(BUILD_DIR)/lib,
# so an app links only -lvulkan and never sees vkclient, g2dclient or pglcompiler.
PRIV = $(BUILD_DIR)/lib/.vulkan_priv

# The GPU transport is compiled ONLY on the Vulkan GPU targets: PGL_G3D=yes AND
# a machine with a real bsp_cl compute back end.  vkclient.c depends on
# <g2dclient/g2dclient.h> (the g2dd IPC transport) and <pglcompiler.h> (the
# on-device mesa compiler contract), which only those machines stage into
# $(SDK_DIR)/include (raspi5's V3D CSD, q8b's Adreno 690 CS).  On a CPU-only
# target this Makefile is a no-op (libvulkan.a stays the software-device
# library).
VK_GPU_HWS := raspi5 q8b
VULKAN_GPU :=
ifeq ($(PGL_G3D),yes)
ifneq ($(filter $(HW),$(VK_GPU_HWS)),)
VULKAN_GPU := yes
endif
endif

VKCLIENT = $(PRIV)/libvkclient.a
SRCS = vkclient.c
OBJS = $(patsubst %.c,$(OBJDIR)/%.o,$(SRCS))

ifeq ($(VULKAN_GPU),yes)

all: $(VKCLIENT)

$(VKCLIENT): $(OBJS)
	@mkdir -p $(dir $(VKCLIENT))
	rm -f $(VKCLIENT)
	$(AR) rcs $(VKCLIENT) $(OBJS)

$(OBJDIR)/%.o: %.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

else

all:
	@echo "vkclient: skipped (CPU-only target: PGL_G3D=$(PGL_G3D) HW=$(HW))"

endif

.PHONY: all clean

clean:
	rm -rf $(ALL_ARCH_DIRS)
	rm -f $(VKCLIENT)
