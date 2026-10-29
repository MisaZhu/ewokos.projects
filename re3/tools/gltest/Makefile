PROJS_ROOT_DIR=../../..
include $(PROJS_ROOT_DIR)/make.inc

TASK = gltest
BUILD_DIR = $(SDK_DIR)/rootfs/apps/$(TASK)

TASK_OBJS = $(ARCH)/gltest.o

EXTRA_LIBS_DIR = $(EWOKOS_SYS_DIR)/build_$(ARCH)/$(HW)
CXXFLAGS += -I $(EXTRA_LIBS_DIR)/include
CFLAGS += -I $(EXTRA_LIBS_DIR)/include
LDFLAGS += -L $(EXTRA_LIBS_DIR)/lib

GLES_LIBS = $(EXTRA_LIBS_DIR)/lib/libglcommon.a \
	$(EXTRA_LIBS_DIR)/lib/libEGL.a \
	$(EXTRA_LIBS_DIR)/lib/libGLESv2.a

all: $(TASK)

$(TASK): $(TASK_OBJS) $(GLES_LIBS) \
		$(SDK_DIR)/lib/libewoksys.a \
		$(SDK_DIR)/lib/libewokstl.a \
		$(SDK_DIR)/lib/libx++.a \
		$(SDK_DIR)/lib/libx.a \
		$(SDK_DIR)/lib/libgraph.a \
		$(SDK_DIR)/lib/libopenlibm.a \
		$(SDK_DIR)/lib/libcxx.a
	mkdir -p $(BUILD_DIR)
	$(LD) -Ttext=100 $(TASK_OBJS) $(GLES_LIBS) -o $(BUILD_DIR)/$(TASK) $(LDFLAGS) $(EWOK_LIB_X) $(EWOK_LIB_GRAPH) -lopenlibm $(EWOK_LIBC) -lcxx

clean:
	rm -rf $(ALL_ARCH_DIRS)

.PHONY: all clean
