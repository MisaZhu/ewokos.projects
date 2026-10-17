PROJS_ROOT_DIR=../../../..
include $(PROJS_ROOT_DIR)/make.inc

BUILD_DIR = $(EWOKOS_SYS_DIR)/build_$(ARCH)/$(HW)

CFLAGS += -Iinclude -I $(TARGET_DIR)/include

LIB_PORTABLEGL_OBJS = $(ARCH)/src/portablegl.o

# The GPU 3D-offload backend is opt-in per platform: a machine with 3D
# hardware acceleration exports PGL_G3D=yes from its system/make.inc, which
# compiles pgl_g3d.o and enables the GPU fast paths in portablegl.c.  The
# backend marshals clears/draws over libg2dclient (headers installed under
# $(SDK_DIR)/include); the g2dd daemon forwards them to the platform 3D driver
# through the bsp_g3d interface.  Platforms without the flag keep the pure
# CPU/NEON rasterizer with no GPU code compiled in.
ifeq ($(PGL_G3D),yes)
CFLAGS += -DPGL_G3D
LIB_PORTABLEGL_OBJS += $(ARCH)/src/pgl_g3d.o $(ARCH)/src/pgl_g3d_shader.o
endif

PORTABLEGL = $(BUILD_DIR)/lib/libportablegl.a

$(PORTABLEGL): $(LIB_PORTABLEGL_OBJS)
	$(AR) rcs $(PORTABLEGL) $(LIB_PORTABLEGL_OBJS)
	cp -r include/* $(BUILD_DIR)/include

# Explicit object rule (mirrors gles/glcommon): the obj dir is created here so a
# clean-tree build never depends on make.rule's implicit $(ARCH)/%.o rule firing.
$(ARCH)/src/%.o: src/%.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

.PHONY: clean

clean:
	rm -rf $(ALL_ARCH_DIRS)
	rm -f src/*.o
	rm -f $(PORTABLEGL)
