PROJS_ROOT_DIR=../../..
include $(PROJS_ROOT_DIR)/make.inc

BUILD_DIR = $(EWOKOS_SYS_DIR)/build_$(ARCH)/$(HW)

# Object dir is keyed by $(HW) as well as $(ARCH), mirroring libs/opencl +
# libs/gles: the GPU gate below preprocesses runtime differently (GPU device
# registration), so encoding $(HW) keeps each machine's objects separate and
# switching HW is always a fresh compile.
OBJDIR = $(ARCH)/$(HW)

# -I../../include : the public Khronos vulkan/*.h headers this lib exports (source
#                   tree, so the lib compiles before the headers are staged)
# -Isrc           : internal.h
# -Isrc/spirv     : the SPIR-V front-end shared headers
CFLAGS += -I../../include -Isrc -Isrc/spirv

# GPU back-end gate.  The raspi5 GPU device is compiled in ONLY on the Vulkan GPU
# target: PGL_G3D=yes AND a raspi5-class HW.  When gated on:
#   - src/gpu is added and VULKAN_GPU_BACKEND is defined, so runtime registers the
#     GPU device (vk_gpu_probe/vk_gpu_device_init in src/gpu/gpu_device.c);
#   - the private vkclient transport + the on-device mesa compiler archives that
#     projects/opengl stages under .gles_priv are ADDLIB-merged into libvulkan.a,
#     so an app still links only -lvulkan (+ -lg2dclient -lewokstl -lcxx).
VK_GPU_HWS := raspi5
VULKAN_GPU :=
ifeq ($(PGL_G3D),yes)
ifneq ($(filter $(HW),$(VK_GPU_HWS)),)
VULKAN_GPU := yes
endif
endif

PRIV_GLES   = $(BUILD_DIR)/lib/.gles_priv
PRIV_VULKAN = $(BUILD_DIR)/lib/.vulkan_priv
VKCLIENT_A  = $(PRIV_VULKAN)/libvkclient.a
PGC_FE_A    = $(PRIV_GLES)/libpglcompiler_fe.a
PGC_BE_A    = $(PRIV_GLES)/libpglcompiler_be.a

# The software-device Vulkan 1.3 runtime (api + runtime + spirv front-end + cpu
# backend).  Every machine builds this same core; the GPU target appends src/gpu.
SRCS = $(wildcard src/api/*.c) \
       $(wildcard src/runtime/*.c) \
       $(wildcard src/spirv/*.c) \
       $(wildcard src/cpu/*.c)

ifeq ($(VULKAN_GPU),yes)
CFLAGS += -DVULKAN_GPU_BACKEND -Isrc/gpu -I../vkclient
SRCS   += $(wildcard src/gpu/*.c)
ifeq ($(filter clean,$(MAKECMDGOALS)),)
ifneq ($(words $(wildcard $(PGC_FE_A) $(PGC_BE_A) $(VKCLIENT_A))),3)
$(error vulkan GPU build: vkclient/pglcompiler archives not all staged (found $(words $(wildcard $(PGC_FE_A) $(PGC_BE_A) $(VKCLIENT_A)))/3) - build projects/opengl and libs/vkclient first)
endif
endif
GPU_MERGE_LIBS = $(VKCLIENT_A) $(PGC_FE_A) $(PGC_BE_A)
else
GPU_MERGE_LIBS =
endif

OBJS = $(patsubst %.c,$(OBJDIR)/%.o,$(SRCS))

VULKAN = $(BUILD_DIR)/lib/libvulkan.a

all: $(VULKAN) install_headers

$(OBJDIR)/%.o: %.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

ifeq ($(VULKAN_GPU),yes)
# GPU target: merge our objects + the private vkclient + mesa compiler archives
# into one libvulkan.a with an ar MRI script (the same idiom libs/opencl uses for
# libOpenCL.a).  Uses $(AR) (the cross ar) exclusively - never mix host/cross ar.
$(VULKAN): $(OBJS) $(GPU_MERGE_LIBS)
	@mkdir -p $(dir $(VULKAN))
	@rm -f $(VULKAN) vulkan_merge.mri
	@{ echo "CREATE $(VULKAN)"; \
	   echo "ADDMOD $(OBJS)"; \
	   for a in $(GPU_MERGE_LIBS); do echo "ADDLIB $$a"; done; \
	   echo "SAVE"; echo "END"; } > vulkan_merge.mri
	$(AR) -M < vulkan_merge.mri
	$(AR) s $(VULKAN)
	@rm -f vulkan_merge.mri
else
$(VULKAN): $(OBJS)
	@mkdir -p $(dir $(VULKAN))
	rm -f $(VULKAN)
	$(AR) rcs $(VULKAN) $(OBJS)
endif

# Stage the Khronos Vulkan headers where apps find them via the standard
# -I$(SDK_DIR)/include.
install_headers:
	@mkdir -p $(BUILD_DIR)/include
	cp -r ../../include/vulkan $(BUILD_DIR)/include
	cp -r ../../include/vk_video $(BUILD_DIR)/include

.PHONY: all clean install_headers

clean:
	rm -rf $(ALL_ARCH_DIRS)
	rm -f $(VULKAN) vulkan_merge.mri
