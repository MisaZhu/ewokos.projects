SW_ROOT_DIR=../../..
include $(SW_ROOT_DIR)/make.inc

BUILD_DIR = $(EWOKOS_SYS_DIR)/build_$(ARCH)/$(HW)
ROOTFS_BIN = $(BUILD_DIR)/rootfs/bin/x

# Object dir is keyed by $(HW), not just $(ARCH).  shadertoy.cpp / cubemap.cpp
# compile a DIFFERENT translation unit under -DPGL_G3D (they call the GPU-only
# pglc_compile_set_cs_local_size), so a shared $(ARCH)/%.o let a raspi5 build
# leave a GPU-shaped object that a later virt build reused against virt's
# CPU-only libGLESv2.a and failed with:
#   undefined reference to `pglc_compile_set_cs_local_size'
# Encoding $(HW) in the path (mirroring BUILD_DIR's build_$(ARCH)/$(HW) layout)
# gives each machine its own objects, so switching HW is always a fresh compile
# and the cpu/gpu trees coexist without a `make clean`.
OBJDIR = $(ARCH)/$(HW)

CFLAGS += -I.. -I../external -I $(BUILD_DIR)/include/SDL2 -D__ewokos__
CXXFLAGS += -I.. -I../external -I $(BUILD_DIR)/include/SDL2 -I../glcommon/include -I../glcommon/include/glcommon -D__ewokos__ -fno-threadsafe-statics

# Common LDFLAGS
LDFLAGS += -lSDL2_image -lSDL2_mixer -lSDL2_gfx -lSDL2_ttf -lSDL2 -lfreetype -lpng -lvorbis -logg -lz -lm -lopenlibm $(EWOK_LIB_X) $(EWOK_LIB_GRAPH) $(EWOK_LIBC) -lcxx

# raspi5 (PGL_G3D=yes): shadertoy.cpp compiles its GLSL ES ports to VideoCore
# VII QPU via pgl_g3d_program_compile.  portablegl and the on-device mesa
# compiler are merged into libGLESv2.a, so the app only needs -DPGL_G3D to see
# the pgl_g3d_* decls; no app-side -lpglcompiler group is required.
#
# Guard on BOTH PGL_G3D=yes AND a GPU-capable HW: virt is CPU-only and its
# merged libGLESv2.a omits pglcompiler_frontend.o (no pglc_compile_set_cs_local_size),
# so activating the app's #ifdef PGL_G3D branch there would leave that symbol
# unresolved at link time.  Filtering by HW also makes the aggregate build safe
# if PGL_G3D=yes leaks in from a machine-level make.inc for a non-GPU target.
GPU_HWS := raspi5 raspix
ifeq ($(PGL_G3D),yes)
ifneq ($(filter $(HW),$(GPU_HWS)),)
CXXFLAGS += -DPGL_G3D
endif
endif

# glcommon library + EGL + merged libGLESv2 (carries portablegl + the mesa
# compiler).  Order matters for static linking: libglcommon and libEGL both
# resolve their gl*/egl* calls against libGLESv2, so it comes last (the same
# glcommon -> EGL -> GLESv2 order apps/gears uses).
GLCOMMON_LIB = $(BUILD_DIR)/lib/libglcommon.a \
	$(BUILD_DIR)/lib/libEGL.a \
	$(BUILD_DIR)/lib/libGLESv2.a
LD_LIB_DEPS = \
	$(GLCOMMON_LIB) \
	$(BUILD_DIR)/lib/libSDL2.a \
	$(BUILD_DIR)/lib/libSDL2_image.a \
	$(BUILD_DIR)/lib/libSDL2_mixer.a \
	$(BUILD_DIR)/lib/libSDL2_gfx.a \
	$(BUILD_DIR)/lib/libSDL2_ttf.a \
	$(BUILD_DIR)/lib/libfreetype.a \
	$(BUILD_DIR)/lib/libpng.a \
	$(BUILD_DIR)/lib/libvorbis.a \
	$(BUILD_DIR)/lib/libogg.a \
	$(BUILD_DIR)/lib/libgif.a \
	$(BUILD_DIR)/lib/libz.a

# C++ files - all refactored to use portablegl's built-in types with PGL_PREFIX_TYPES
# Now all demos compile with C-style code using pgl_compat.h
CPP_SRCS = shadertoy.cpp sphere.cpp cubemap.cpp grass.cpp

C_SRCS = balls.c

# Object files
C_OBJS = $(patsubst %.c,$(OBJDIR)/%.o,$(C_SRCS))
CPP_OBJS = $(patsubst %.cpp,$(OBJDIR)/%.o,$(CPP_SRCS))

# Targets in rootfs
C_BINS = $(patsubst %.c,$(ROOTFS_BIN)/%,$(C_SRCS))
CPP_BINS = $(patsubst %.cpp,$(ROOTFS_BIN)/%,$(CPP_SRCS))

ALL_BINS = $(C_BINS) $(CPP_BINS)

.PHONY: all clean dirs

all: dirs $(ALL_BINS)

dirs:
	@mkdir -p $(ROOTFS_BIN)

# Compile C files
$(OBJDIR)/%.o: %.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -I../glcommon/include -I../glcommon/include/glcommon -I../gles/portablegl/include -I../gles/portablegl/include/portablegl -c $< -o $@

# Compile C++ files (include glcommon for rsw_math and gltools)
$(OBJDIR)/%.o: %.cpp
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) -I../gles/portablegl/include -I../gles/portablegl/include/portablegl -I../glcommon/include -I../glcommon/include/glcommon -I $(BUILD_DIR)/include -c $< -o $@

# Link C programs
$(ROOTFS_BIN)/%: $(OBJDIR)/%.o $(LD_LIB_DEPS) \
		$(SDK_DIR)/lib/libewoksys.a \
		$(SDK_DIR)/lib/libewokstl.a \
		$(SDK_DIR)/lib/libx++.a \
		$(SDK_DIR)/lib/libx.a \
		$(SDK_DIR)/lib/libfont.a \
		$(SDK_DIR)/lib/libgraph.a \
		$(SDK_DIR)/lib/libwidget++.a \
		$(SDK_DIR)/lib/libcxx.a
	$(LD) -Ttext=100 $< $(GLCOMMON_LIB) -o $@ $(LDFLAGS)

clean:
	rm -rf $(ALL_ARCH_DIRS)
	rm -f *.o
	rm -f $(ALL_BINS)
