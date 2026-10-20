PROJS_ROOT_DIR=../../..
include $(PROJS_ROOT_DIR)/make.inc

# soft3d supplies libGLESv2 / libEGL / libimgui under the shared build include
# + lib dir for the current ARCH/HW.  Reuse it rather than duplicating the GL
# front end.
SOFT3D_DIR = $(PROJS_ROOT_DIR)/soft3d

TASK_SRCS = main.cpp theme.cpp scene.cpp viewport.cpp ui.cpp
TASK_OBJS = $(patsubst %.cpp,$(ARCH)/%.o,$(TASK_SRCS))

EXTRA_LIBS_DIR = $(EWOKOS_SYS_DIR)/build_$(ARCH)/$(HW)

CFLAGS   += -I. -I $(EXTRA_LIBS_DIR)/include -I $(EXTRA_LIBS_DIR)/include/imgui -D__ewokos__
CXXFLAGS += -I. -I $(EXTRA_LIBS_DIR)/include -I $(EXTRA_LIBS_DIR)/include/imgui -D__ewokos__ -fno-threadsafe-statics -fno-exceptions -fno-rtti
LDFLAGS  += -L $(EXTRA_LIBS_DIR)/lib

# Match apps/imgui_demo: libimgui -> libEGL -> libGLESv2 (imgui backend calls
# gl* / egl*, and libGLESv2 carries the engine + optional v3dcompiler).
IMGUI_LIBS = $(EXTRA_LIBS_DIR)/lib/libimgui.a \
	$(EXTRA_LIBS_DIR)/lib/libEGL.a \
	$(EXTRA_LIBS_DIR)/lib/libGLESv2.a

TASK = blender
TASK_DIR = $(EWOKOS_SYS_DIR)/build_$(ARCH)/$(HW)/rootfs/apps/$(TASK)

all: $(TASK)

$(TASK): $(TASK_OBJS) $(IMGUI_LIBS) \
		$(SDK_DIR)/lib/libewoksys.a \
		$(SDK_DIR)/lib/libewokstl.a \
		$(SDK_DIR)/lib/libx++.a \
		$(SDK_DIR)/lib/libx.a \
		$(SDK_DIR)/lib/libfont.a \
		$(SDK_DIR)/lib/libgraph.a \
		$(SDK_DIR)/lib/libwidget++.a \
		$(SDK_DIR)/lib/libcxx.a
	mkdir -p $(TASK_DIR)
	$(LD) -Ttext=100 $(TASK_OBJS) $(IMGUI_LIBS) -o $(TASK_DIR)/$(TASK) $(LDFLAGS) $(EWOK_LIB_X) $(EWOK_LIB_GRAPH) $(EWOK_LIBC) -lcxx
	cp -r res $(TASK_DIR)

$(ARCH)/%.o: %.cpp
	@mkdir -p $(ARCH)
	$(CXX) $(CXXFLAGS) -c $< -o $@

clean:
	rm -rf $(ALL_ARCH_DIRS)
	rm -f *.o
