PROJS_ROOT_DIR=../../..
include $(PROJS_ROOT_DIR)/make.inc

BUILD_DIR = $(EWOKOS_SYS_DIR)/build_$(ARCH)/$(HW)

# Object dir is keyed by $(HW) as well as $(ARCH), mirroring libs/gles: Phase 2
# gates the GPU backend on the machine's PGL_G3D=yes, which preprocesses
# runtime.c differently (cl_gpu_probe registration).  Encoding $(HW) keeps each
# machine's objects separate so switching HW is always a fresh compile.
OBJDIR = $(ARCH)/$(HW)

# -I../../include : the public CL headers this lib exports (source tree, so the
#                   lib compiles before the headers are staged into BUILD_DIR)
# -Isrc           : internal.h
# -Isrc/clc       : the OpenCL-C front-end shared headers
CFLAGS += -I../../include -Isrc -Isrc/clc

# Phase 1: the CPU-complete OpenCL 1.2 runtime.  Every machine builds the same
# object set (api + runtime + clc front-end + cpu backend); there is no code
# divergence for machines without a GPU.  Phase 2 appends src/gpu and defines
# OPENCL_GPU_BACKEND under PGL_G3D=yes.
SRCS = $(wildcard src/api/*.c) \
       $(wildcard src/runtime/*.c) \
       $(wildcard src/clc/*.c) \
       $(wildcard src/cpu/*.c)

OBJS = $(patsubst %.c,$(OBJDIR)/%.o,$(SRCS))

OPENCL = $(BUILD_DIR)/lib/libOpenCL.a

all: $(OPENCL) install_headers

$(OBJDIR)/%.o: %.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

$(OPENCL): $(OBJS)
	@mkdir -p $(dir $(OPENCL))
	rm -f $(OPENCL)
	$(AR) rcs $(OPENCL) $(OBJS)

# Stage the Khronos CL headers where apps (and the app-side GPU compiler in
# Phase 2) find them via the standard -I$(SDK_DIR)/include.
install_headers:
	@mkdir -p $(BUILD_DIR)/include
	cp -r ../../include/CL $(BUILD_DIR)/include

.PHONY: all clean install_headers

clean:
	rm -rf $(ALL_ARCH_DIRS)
	rm -f $(OPENCL)
