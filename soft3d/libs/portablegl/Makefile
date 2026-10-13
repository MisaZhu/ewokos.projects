PROJS_ROOT_DIR=../../..
include $(PROJS_ROOT_DIR)/make.inc

BUILD_DIR = $(EWOKOS_SYS_DIR)/build_$(ARCH)/$(HW)

CFLAGS += -Iinclude -I $(TARGET_DIR)/include

LIB_PORTABLEGL_OBJS = $(ARCH)/src/portablegl.o

# raspi5 only: compile the VideoCore V3D 3D-pipeline GPU backend and enable
# the GPU fast paths in portablegl.c.  The backend drives /dev/g2d through
# libg2dclient (headers installed under $(SDK_DIR)/include), which the raspi5
# g2dd daemon forwards to the V3D 3D driver in libvideocore.  Every other HW
# keeps the pure CPU/NEON rasterizer with no GPU code compiled in.
ifeq ($(HW),raspi5)
CFLAGS += -DPGL_VC3D
LIB_PORTABLEGL_OBJS += $(ARCH)/src/pgl_vc3d.o
endif

PORTABLEGL = $(BUILD_DIR)/lib/libportablegl.a

$(PORTABLEGL): $(LIB_PORTABLEGL_OBJS)
	$(AR) rcs $(PORTABLEGL) $(LIB_PORTABLEGL_OBJS)
	cp -r include/* $(BUILD_DIR)/include

.PHONY: clean

clean:
	rm -rf $(ALL_ARCH_DIRS)
	rm -f src/*.o
	rm -f $(PORTABLEGL)
