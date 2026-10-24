PROJS_ROOT_DIR=../../..
include $(PROJS_ROOT_DIR)/make.inc

BUILD_DIR = $(EWOKOS_SYS_DIR)/build_$(ARCH)/$(HW)

CFLAGS += -I $(BUILD_DIR)/include
LDFLAGS += -L $(BUILD_DIR)/lib

TASK = clinfo
TASK_OBJS = $(ARCH)/$(TASK).o
TASK_DIR = $(BUILD_DIR)/rootfs/apps/$(TASK)
OPENCL_LIB = $(BUILD_DIR)/lib/libOpenCL.a

all: $(TASK)

$(TASK): $(TASK_OBJS) $(OPENCL_LIB) \
         $(SDK_DIR)/lib/libewoksys.a $(SDK_DIR)/lib/libopenlibm.a
	mkdir -p $(TASK_DIR)
	$(LD) -Ttext=100 $(TASK_OBJS) $(OPENCL_LIB) -o $(TASK_DIR)/$(TASK) $(LDFLAGS) -lopenlibm $(EWOK_LIBC)

clean:
	rm -rf $(ALL_ARCH_DIRS)
	rm -f *.o

.PHONY: all clean
