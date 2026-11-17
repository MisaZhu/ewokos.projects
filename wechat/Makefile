PROJS_ROOT_DIR=..
include $(PROJS_ROOT_DIR)/make.inc

.DEFAULT_GOAL := all

BUILD_DIR = $(EWOKOS_SYS_DIR)/build_$(ARCH)/$(HW)
TARGET_DIR = $(BUILD_DIR)/rootfs

LDFLAGS = -L $(TARGET_DIR)/lib -L $(BUILD_DIR)/lib

CXXFLAGS += -I $(BUILD_DIR)/include -I src

WECHAT_OBJS = \
	$(ARCH)/src/main.o \
	$(ARCH)/src/Backend.o \
	$(ARCH)/src/LocalBackend.o \
	$(ARCH)/src/AvatarCache.o \
	$(ARCH)/src/LoginWidget.o \
	$(ARCH)/src/SessionList.o \
	$(ARCH)/src/ContactList.o \
	$(ARCH)/src/ChatWidget.o \
	$(ARCH)/src/ImageViewWin.o

LD_LIB_DEPS = \
	$(BUILD_DIR)/lib/libwidget++.a \
	$(BUILD_DIR)/lib/libx++.a \
	$(BUILD_DIR)/lib/libx.a \
	$(BUILD_DIR)/lib/libtinyjson.a \
	$(BUILD_DIR)/lib/libewokstl.a \
	$(BUILD_DIR)/lib/libfont.a \
	$(BUILD_DIR)/lib/libfreetype.a \
	$(BUILD_DIR)/lib/libiconbuf.a \
	$(BUILD_DIR)/lib/libgraph.a \
	$(BUILD_DIR)/lib/libpng.a \
	$(BUILD_DIR)/lib/libjpeg.a \
	$(BUILD_DIR)/lib/libgif.a \
	$(BUILD_DIR)/lib/libtga.a \
	$(BUILD_DIR)/lib/libsvg.a \
	$(BUILD_DIR)/lib/libz.a \
	$(BUILD_DIR)/lib/libdisplayman.a \
	$(BUILD_DIR)/lib/libdisplay.a \
	$(BUILD_DIR)/lib/libkeyb.a \
	$(BUILD_DIR)/lib/libmouse.a \
	$(BUILD_DIR)/lib/libclipboard.a \
	$(BUILD_DIR)/lib/libewoksys.a \
	$(BUILD_DIR)/lib/libc.a \
	$(BUILD_DIR)/lib/libgloss.a \
	$(BUILD_DIR)/lib/libcxx.a

WECHAT_DIR = $(TARGET_DIR)/apps/wechat
WECHAT = $(WECHAT_DIR)/wechat

all: $(WECHAT)

$(WECHAT): $(WECHAT_OBJS) $(LD_LIB_DEPS)
	mkdir -p $(WECHAT_DIR)
	$(LD) -Ttext=100 $(WECHAT_OBJS) -o $(WECHAT) $(LDFLAGS) \
		$(EWOK_LIB_X) \
		$(EWOK_LIB_GRAPH) \
		$(EWOK_LIBC) -lcxx
	cp -r res $(WECHAT_DIR)

$(ARCH)/%.o: %.cc
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) -c $< -o $@

clean:
	rm -f $(WECHAT_OBJS)
	rm -fr $(WECHAT_DIR)