# re3 (reverse-engineered GTA III) ported to EwokOS.
#
# Upstream: https://github.com/GTAmodding/re3 (vendored under src/, with
# aap/librw vendored under vendor/librw). The EwokOS-specific pieces are:
#   - src/skel/ewok/ewok.cpp     platform skeleton (libx window, input, timer)
#   - src/skel/crossplatform.h   __ewokos__ branches (mouse shims, void* window)
#   - src/core/config.h          __ewokos__ config block (NO_MOVIES, no colourfilter)
#   - src/core/Pad.cpp           __ewokos__ branches calling the mouse shims
#   - vendor/librw gl3 device    LIBRW_EWOKOS branch: xwin + EGL GLES2 context
#
# Rendering: librw gl3 in GLES2 mode over the EwokOS EGL/libGLESv2 stack
# (portablegl; GPU-accelerated on machines with a real GL backend such as the
# Raspberry Pi 5 V3D route). Audio: sampman_null (silent). On EwokOS, game
# data is read from /data/gta3 when present; the executable remains under
# /apps/re3 (see README.md).
#
# This needs the opengl project built first, so that libEGL/libGLESv2 and the
# GLES2 headers are staged into the shared build_$(ARCH)/$(HW) tree
# (projects/Makefile enforces re3: opengl).

PROJS_ROOT_DIR=..
include $(PROJS_ROOT_DIR)/make.inc

TASK = re3
BUILD_DIR = $(SDK_DIR)/rootfs/apps/$(TASK)

# ---- compile flags ----------------------------------------------------------

RE3_DEFINES = -DNDEBUG -DLIBRW -DRW_GL3 -DLIBRW_EWOKOS -DUSE_OUR_VERSIONING -DCMAKE_NO_AUTOLINK
LIBRW_DEFINES = -DNDEBUG -DRW_GL3 -DLIBRW_EWOKOS -DLODEPNG_NO_COMPILE_CPP

# -fno-threadsafe-statics: function-local statics (re3 uses many) would pull in
# __cxa_guard_* otherwise. The rest silences upstream-warning noise that the
# project does not treat as errors.
RE3_WFLAGS = -fno-threadsafe-statics -Wno-pedantic -Wno-sign-compare \
	-Wno-unused-parameter -Wno-reorder -Wno-unused-variable \
	-Wno-unused-but-set-variable -Wno-maybe-uninitialized \
	-Wno-double-promotion -Wno-missing-field-initializers -Wno-narrowing

CXXFLAGS += $(RE3_DEFINES) $(RE3_WFLAGS)
CFLAGS += $(LIBRW_DEFINES) -Wno-unused-parameter

# every re3 source dir is an include dir (same as the upstream CMake layout)
RE3_INC := $(shell find src -type d | sed 's|^|-I|')
LIBRW_INC := -I vendor/librw/src -I vendor/librw

CXXFLAGS += $(RE3_INC) $(LIBRW_INC)
CFLAGS += $(LIBRW_INC)

# ---- sources ----------------------------------------------------------------

# The full game tree except: other platform skeletons (glfw/win) and the
# audio backends we cannot build (OpenAL, Miles) plus their EAX helpers.
# With neither AUDIO_OAL nor AUDIO_MSS defined the game compiles against
# sampman_null.
GAME_SRCS := $(shell find src -name '*.cpp' | sed 's|^src/||')
GAME_SRCS := $(filter-out \
	skel/glfw/% skel/win/% \
	audio/sampman_oal.cpp audio/sampman_miles.cpp audio/oal/% audio/eax/% \
	,$(GAME_SRCS))

# librw: portable core + every platform backend + lodepng + glad loader.
# Engine::init() calls each backend's registerPlatformPlugins(), so all of
# them must be in the binary even though only gl3 is active under RW_GL3
# (same as the upstream CMake build).
LIBRW_CORE_SRCS := anim base bmp camera charset clump engine error frame \
	geometry geoplg hanim image light matfx pipeline plg png prim raster \
	render skin texture tga tristrip userdata uvanim world
LIBRW_GL_SRCS := gl/gl3 gl/gl3device gl/gl3immed gl/gl3matfx gl/gl3pipe \
	gl/gl3raster gl/gl3render gl/gl3shader gl/gl3skin gl/wdgl gl/ewokglprocs \
	gl/glad/glad lodepng/lodepng
LIBRW_D3D_SRCS := d3d/d3d d3d/d3d8 d3d/d3d8matfx d3d/d3d8render d3d/d3d8skin \
	d3d/d3d9 d3d/d3d9matfx d3d/d3d9render d3d/d3d9skin d3d/d3ddevice \
	d3d/d3dimmed d3d/d3drender d3d/xbox d3d/xboxmatfx \
	d3d/xboxskin d3d/xboxvfmt
LIBRW_PS2_SRCS := ps2/pds ps2/ps2 ps2/ps2device ps2/ps2matfx ps2/ps2raster \
	ps2/ps2skin

GAME_OBJS = $(addprefix $(ARCH)/game/,$(GAME_SRCS:.cpp=.o))
LIBRW_OBJS = $(addprefix $(ARCH)/librw/,$(addsuffix .o,$(LIBRW_CORE_SRCS) $(LIBRW_GL_SRCS) $(LIBRW_D3D_SRCS) $(LIBRW_PS2_SRCS)))

TASK_OBJS = $(GAME_OBJS) $(LIBRW_OBJS)

# ---- build rules ------------------------------------------------------------

.DEFAULT_GOAL := all

# make.rule provides $(ARCH)/%.o pattern rules; ours mirror the source tree
# under $(ARCH)/game and $(ARCH)/librw so object paths never collide.
$(ARCH)/game/%.o: src/%.cpp
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) -c $< -o $@

$(ARCH)/librw/%.o: vendor/librw/src/%.cpp
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) -c $< -o $@

$(ARCH)/librw/%.o: vendor/librw/src/%.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

GLES_LIBS = $(SDK_DIR)/lib/libglcommon.a \
	$(SDK_DIR)/lib/libEGL.a \
	$(SDK_DIR)/lib/libGLESv2.a

all: $(TASK)

$(TASK): $(TASK_OBJS) $(GLES_LIBS) \
		$(SDK_DIR)/lib/libewoksys.a \
		$(SDK_DIR)/lib/libewokstl.a \
		$(SDK_DIR)/lib/libx++.a \
		$(SDK_DIR)/lib/libx.a \
		$(SDK_DIR)/lib/libfont.a \
		$(SDK_DIR)/lib/libgraph.a \
		$(SDK_DIR)/lib/libwidget++.a \
		$(SDK_DIR)/lib/libopenlibm.a \
		$(SDK_DIR)/lib/libcxx.a
	mkdir -p $(BUILD_DIR)
	$(LD) -Ttext=100 $(TASK_OBJS) $(GLES_LIBS) -o $(BUILD_DIR)/$(TASK) $(LDFLAGS) $(EWOK_LIB_X) $(EWOK_LIB_GRAPH) -lopenlibm $(EWOK_LIBC) -lcxx
	# gamefiles/ are the freely distributable re3 data files; the original
	# GTA III assets (models/gta3.img, audio, ...) must be supplied by the
	# user (see README.md).
	cp -r gamefiles/. $(BUILD_DIR)/

clean:
	rm -rf $(ALL_ARCH_DIRS)

.PHONY: all clean
