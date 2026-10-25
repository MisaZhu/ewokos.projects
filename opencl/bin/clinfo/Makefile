PROJS_ROOT_DIR=../../..
include $(PROJS_ROOT_DIR)/make.inc

BUILD_DIR = $(EWOKOS_SYS_DIR)/build_$(ARCH)/$(HW)

CFLAGS += -I $(BUILD_DIR)/include
LDFLAGS += -L $(BUILD_DIR)/lib

TASK = clinfo
TASK_OBJS = $(ARCH)/$(TASK).o
TASK_DIR = $(BUILD_DIR)/rootfs/bin
OPENCL_LIB = $(BUILD_DIR)/lib/libOpenCL.a

# Phase 2 GPU targets (raspi5 V3D CSD, q8b Adreno 690 CS): libOpenCL.a
# additionally folds in clclient + the on-device mesa compiler, so the command
# must also link the g2d transport and the C++ runtime mesa needs (operator
# new[]/delete[] + the STL from libewokstl, the cxa guards from libcxx).  Gated
# on the same PGL_G3D=yes + CL-GPU-HW condition libs/opencl uses, so a CPU-only
# target keeps its exact link line (the extra archives would be unreferenced
# no-ops there anyway).  Order mirrors the proven GLES app closure:
# transport/STL before $(EWOK_LIBC), -lcxx last.
CL_GPU_HWS := raspi5 q8b
OPENCL_GPU_LIBS :=
OPENCL_GPU_CXX :=
ifeq ($(PGL_G3D),yes)
ifneq ($(filter $(HW),$(CL_GPU_HWS)),)
OPENCL_GPU_LIBS := -lg2dclient -lewokstl
OPENCL_GPU_CXX := -lcxx
endif
endif

all: $(TASK)

$(TASK): $(TASK_OBJS) $(OPENCL_LIB) \
         $(SDK_DIR)/lib/libewoksys.a $(SDK_DIR)/lib/libopenlibm.a
	mkdir -p $(TASK_DIR)
	$(LD) -Ttext=100 $(TASK_OBJS) $(OPENCL_LIB) -o $(TASK_DIR)/$(TASK) $(LDFLAGS) -lopenlibm $(OPENCL_GPU_LIBS) $(EWOK_LIBC) $(OPENCL_GPU_CXX)

clean:
	rm -rf $(ALL_ARCH_DIRS)
	rm -f *.o

.PHONY: all clean
