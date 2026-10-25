PROJS_ROOT_DIR=../../..
include $(PROJS_ROOT_DIR)/make.inc

BUILD_DIR = $(EWOKOS_SYS_DIR)/build_$(ARCH)/$(HW)

# Object dir keyed by $(HW) as well as $(ARCH) (mirrors libs/opencl + libs/gles):
# the GPU gate below preprocesses differently per machine, so each machine gets
# its own objects and switching HW is always a fresh compile.
OBJDIR = $(ARCH)/$(HW)

# clclient is a PRIVATE intermediate: libclclient.a is folded into libOpenCL.a by
# libs/opencl/Makefile (an ar ADDLIB merge), never installed into $(BUILD_DIR)/lib,
# so an app links only -lOpenCL and never sees clclient, g2dclient or pglcompiler.
PRIV = $(BUILD_DIR)/lib/.opencl_priv

# The GPU transport is compiled ONLY on the OpenCL GPU target: PGL_G3D=yes AND a
# raspi5-class HW.  clclient.c depends on <g2dclient/g2dclient.h> (the g2dd IPC
# transport) and <pglcompiler.h> (the on-device mesa compiler contract), which
# only those machines stage into $(SDK_DIR)/include.  raspix/q8b also export
# PGL_G3D=yes but are not OpenCL GPU targets yet, so the HW filter keeps them --
# and virt -- CPU-only, matching libs/opencl's gate exactly.  On a CPU-only
# target this Makefile is a no-op (libOpenCL.a stays the Phase-1 CPU library).
CL_GPU_HWS := raspi5
OPENCL_GPU :=
ifeq ($(PGL_G3D),yes)
ifneq ($(filter $(HW),$(CL_GPU_HWS)),)
OPENCL_GPU := yes
endif
endif

CLCLIENT = $(PRIV)/libclclient.a
SRCS = clclient.c
OBJS = $(patsubst %.c,$(OBJDIR)/%.o,$(SRCS))

ifeq ($(OPENCL_GPU),yes)

all: $(CLCLIENT)

$(CLCLIENT): $(OBJS)
	@mkdir -p $(dir $(CLCLIENT))
	rm -f $(CLCLIENT)
	$(AR) rcs $(CLCLIENT) $(OBJS)

$(OBJDIR)/%.o: %.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

else

all:
	@echo "clclient: skipped (CPU-only target: PGL_G3D=$(PGL_G3D) HW=$(HW))"

endif

.PHONY: all clean

clean:
	rm -rf $(ALL_ARCH_DIRS)
	rm -f $(CLCLIENT)
