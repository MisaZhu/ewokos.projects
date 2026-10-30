include ../make.inc

BUILD_DIR = $(SDK_DIR)
TARGET_DIR = $(BUILD_DIR)/rootfs
APP_DIR = $(TARGET_DIR)/bin
TARGET = $(APP_DIR)/install

GUI_TASK = xinstall
GUI_DIR = $(TARGET_DIR)/apps/$(GUI_TASK)
GUI_TARGET = $(GUI_DIR)/$(GUI_TASK)

# The installer links the machine BSP directly: the q8b backend drives the
# SDCC2 card and the M.2 NVMe through the user-space drivers that
# machines/q8b/system/libs/bsp stages into $(SDK_DIR).  No filesystem daemon
# is involved; every copy is raw-sector IO, same model as sdupdate/espwrite.
CORE_OBJS = $(ARCH)/src/gpt.o \
	$(ARCH)/src/copy.o \
	$(ARCH)/src/clone.o \
	$(ARCH)/src/mach/mach.o \
	$(ARCH)/src/mach/q8b.o

CLI_OBJS = $(ARCH)/src/main.o
GUI_OBJS = $(ARCH)/gui/xinstall.o

CFLAGS += -I include
CXXFLAGS += -I include -I src

all: $(TARGET) $(GUI_TARGET)

$(TARGET): $(CLI_OBJS) $(CORE_OBJS) \
		$(SDK_DIR)/lib/libbsp.a \
		$(SDK_DIR)/lib/libsd.a \
		$(SDK_DIR)/lib/libewoksys.a
	@mkdir -p $(dir $@)
	$(LD) -Ttext=100 $(CLI_OBJS) $(CORE_OBJS) -o $@ $(LDFLAGS) \
		-lbsp -lsd $(EWOK_LIBC)

# same engine + drivers, widget++ front end (same link shape as wDemo)
$(GUI_TARGET): $(GUI_OBJS) $(CORE_OBJS) \
		$(SDK_DIR)/lib/libewoksys.a \
		$(SDK_DIR)/lib/libewokstl.a \
		$(SDK_DIR)/lib/libx++.a \
		$(SDK_DIR)/lib/libx.a \
		$(SDK_DIR)/lib/libfont.a \
		$(SDK_DIR)/lib/libgraph.a \
		$(SDK_DIR)/lib/libwidget++.a \
		$(SDK_DIR)/lib/libbsp.a \
		$(SDK_DIR)/lib/libsd.a \
		$(SDK_DIR)/lib/libcxx.a
	@mkdir -p $(GUI_DIR)
	$(LD) -Ttext=100 $(GUI_OBJS) $(CORE_OBJS) -o $@ $(LDFLAGS) \
		$(EWOK_LIB_X) $(EWOK_LIB_GRAPH) -lbsp -lsd $(EWOK_LIBC) -lcxx

clean:
	rm -f $(CORE_OBJS) $(CLI_OBJS) $(GUI_OBJS)
	rm -f $(CORE_OBJS:.o=.d) $(CLI_OBJS:.o=.d) $(GUI_OBJS:.o=.d)
	rm -f $(TARGET) $(GUI_TARGET)

.PHONY: all clean
