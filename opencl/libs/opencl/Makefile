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

# Phase 2 GPU front-end gate.  The GPU device is compiled in ONLY on the OpenCL
# GPU targets: PGL_G3D=yes AND a machine with a real bsp_cl compute back end
# (raspi5's V3D CSD, q8b's Adreno 690 CS).  raspix exports PGL_G3D=yes but is
# not an OpenCL GPU target yet, so the HW filter keeps it -- and virt -- on the
# CPU-only Phase-1 build (matching libs/clclient's identical gate).  When gated on:
#   - src/gpu is added and OPENCL_GPU_BACKEND is defined, so runtime.c registers
#     the GPU device (cl_gpu_probe/cl_gpu_device_init in src/gpu/gpu_device.c);
#   - the private clclient transport + the on-device mesa compiler archives that
#     projects/opengl stages under .gles_priv are ADDLIB-merged into libOpenCL.a,
#     so an app still links only -lOpenCL (+ -lg2dclient -lewokstl -lcxx).
CL_GPU_HWS := raspi5 q8b
OPENCL_GPU :=
ifeq ($(PGL_G3D),yes)
ifneq ($(filter $(HW),$(CL_GPU_HWS)),)
OPENCL_GPU := yes
endif
endif

PRIV_GLES   = $(BUILD_DIR)/lib/.gles_priv
PRIV_OPENCL = $(BUILD_DIR)/lib/.opencl_priv
CLCLIENT_A  = $(PRIV_OPENCL)/libclclient.a
PGC_FE_A    = $(PRIV_GLES)/libpglcompiler_fe.a
PGC_BE_A    = $(PRIV_GLES)/libpglcompiler_be.a

# Phase 1: the CPU-complete OpenCL 1.2 runtime (api + runtime + clc front-end +
# cpu backend).  Every machine builds this same core; Phase 2 appends src/gpu.
SRCS = $(wildcard src/api/*.c) \
       $(wildcard src/runtime/*.c) \
       $(wildcard src/clc/*.c) \
       $(wildcard src/cpu/*.c)

# projects/opengl (ordered before opencl by projects/Makefile) builds the mesa
# compiler archives; libs/clclient (ordered before opencl by libs/Makefile)
# builds the transport.  Fail loudly if a GPU build runs before they are staged.
# Skip the guard for `make clean`: libs/Makefile cleans clclient BEFORE opencl,
# so libclclient.a is already gone when this Makefile parses, and clean needs
# none of the merged archives (only the $(OPENCL) link rule does).  Without this
# MAKECMDGOALS check `make clean` would abort here with a false 2/3 staging error.
ifeq ($(OPENCL_GPU),yes)
CFLAGS += -DOPENCL_GPU_BACKEND -Isrc/gpu -I../clclient
SRCS   += $(wildcard src/gpu/*.c)
ifeq ($(filter clean,$(MAKECMDGOALS)),)
ifneq ($(words $(wildcard $(PGC_FE_A) $(PGC_BE_A) $(CLCLIENT_A))),3)
$(error opencl GPU build: clclient/pglcompiler archives not all staged (found $(words $(wildcard $(PGC_FE_A) $(PGC_BE_A) $(CLCLIENT_A)))/3) - build projects/opengl and libs/clclient first)
endif
endif
GPU_MERGE_LIBS = $(CLCLIENT_A) $(PGC_FE_A) $(PGC_BE_A)
else
GPU_MERGE_LIBS =
endif

OBJS = $(patsubst %.c,$(OBJDIR)/%.o,$(SRCS))

OPENCL = $(BUILD_DIR)/lib/libOpenCL.a

all: $(OPENCL) install_headers

$(OBJDIR)/%.o: %.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

ifeq ($(OPENCL_GPU),yes)
# GPU target: merge our objects + the private clclient + mesa compiler archives
# into one libOpenCL.a with an ar MRI script (the same idiom libs/gles uses for
# libGLESv2.a).  ld rescans a lone archive until no new member resolves, so
# mesa's internal frontend<->backend circular refs settle without an app-side
# --start-group; mesa's libc/libm/cxx refs are forward refs to the system
# archives that follow libOpenCL.a on the app link line.  Uses $(AR) (the cross
# ar) exclusively - never mix host/cross ar on one archive.
$(OPENCL): $(OBJS) $(GPU_MERGE_LIBS)
	@mkdir -p $(dir $(OPENCL))
	@rm -f $(OPENCL) opencl_merge.mri
	@{ echo "CREATE $(OPENCL)"; \
	   echo "ADDMOD $(OBJS)"; \
	   for a in $(GPU_MERGE_LIBS); do echo "ADDLIB $$a"; done; \
	   echo "SAVE"; echo "END"; } > opencl_merge.mri
	$(AR) -M < opencl_merge.mri
	$(AR) s $(OPENCL)
	@rm -f opencl_merge.mri
else
$(OPENCL): $(OBJS)
	@mkdir -p $(dir $(OPENCL))
	rm -f $(OPENCL)
	$(AR) rcs $(OPENCL) $(OBJS)
endif

# Stage the Khronos CL headers where apps (and the app-side GPU compiler in
# Phase 2) find them via the standard -I$(SDK_DIR)/include.
install_headers:
	@mkdir -p $(BUILD_DIR)/include
	cp -r ../../include/CL $(BUILD_DIR)/include

.PHONY: all clean install_headers

clean:
	rm -rf $(ALL_ARCH_DIRS)
	rm -f $(OPENCL) opencl_merge.mri
