PROJS_ROOT_DIR=..
include $(PROJS_ROOT_DIR)/make.inc
# make.rule clears .DEFAULT_GOAL (dep-file hijack guard); restore ours.
.DEFAULT_GOAL := all

BUILD_DIR = $(EWOKOS_SYS_DIR)/build_$(ARCH)/$(HW)

# This library (moved out of projects/opengl/libs) now builds TWO archives:
#   libimgui_gles.a - imgui core + backends/imgui_impl_gles2.cpp (GLES2 front end)
#   libimgui_vk.a   - imgui core + backends/imgui_impl_vulkan.cpp (Vulkan front end)
# Each archive gets its own object tree ($(ARCH)/gles, $(ARCH)/vk): the two
# variants carry different include paths (<GLES2/gl2.h> vs <vulkan/vulkan.h>)
# and must never share objects.
#
# Both variants compile straight from the Khronos headers in the SIBLING SOURCE
# trees (../opengl/libs/gles/include, ../vulkan/include), NOT from the staged
# $(BUILD_DIR)/include copies.  This keeps the project graph cycle-free: imgui
# needs no upstream project to be BUILT (only their checked-in headers), so it
# carries no prerequisites, while its consumers (projects/opengl/apps and
# projects/blender, which link libimgui_gles.a) are ordered AFTER imgui in
# projects/Makefile.  Gating each variant on its source header's presence means
# a tree missing one Khronos subtree builds the other variant instead of dying.

# Prepend this library's parent AND its own dir so both include forms resolve
# to this library's OWN source headers, never a previously staged copy under
# $(BUILD_DIR)/include (which make.inc adds via -I $(SDK_DIR)/include):
#   - backends/*.h include "imgui/imgui.h" (prefix form) -> needs -I.. (=projects)
#   - backends/*.cpp include "imgui.h" (plain)            -> needs -I.  (=projects/imgui)
# Both resolve to the SAME source inode, so imgui.h's #pragma once dedups them.
# The staged copy is a distinct inode; if it wins, imgui.h double-parses and
# triggers "redefinition of ImGui::...".  ':=' (not '+=' ) is REQUIRED: it puts
# -I.. / -I. ahead of make.inc's -I $(SDK_DIR)/include so the source tree wins.
# A library must build from its own sources, never from a stale copy of itself.
IMGUI_INCLUDES = -I.. -I.
CXXFLAGS := $(IMGUI_INCLUDES) $(CXXFLAGS)

# ImGui C++ core source files (shared by both variants; imgui_demo.cpp stays
# out of the archives, same as before).
IMGUI_CORE_SRCS = imgui.cpp imgui_draw.cpp imgui_tables.cpp imgui_widgets.cpp

# Khronos header source trees (checked-in, always present; NOT the staged SDK
# copies) -- see the cycle-free note above.
GLES_HDR_DIR = ../opengl/libs/gles/include   # GLES2/gl2.h + KHR/khrplatform.h
VK_HDR_DIR   = ../vulkan/include             # vulkan/vulkan.h

# ---- GLES2 variant ----------------------------------------------------------
GLES_CXXFLAGS = $(CXXFLAGS) -I$(GLES_HDR_DIR)
GLES_CORE_OBJS = $(patsubst %.cpp,$(ARCH)/gles/%.o,$(IMGUI_CORE_SRCS))
GLES_OBJS = $(GLES_CORE_OBJS) $(ARCH)/gles/backends/imgui_impl_gles2.o
IMGUI_GLES = $(BUILD_DIR)/lib/libimgui_gles.a

# ---- Vulkan variant ---------------------------------------------------------
VK_CXXFLAGS = $(CXXFLAGS) -I$(VK_HDR_DIR)
VK_CORE_OBJS = $(patsubst %.cpp,$(ARCH)/vk/%.o,$(IMGUI_CORE_SRCS))
VK_OBJS = $(VK_CORE_OBJS) $(ARCH)/vk/backends/imgui_impl_vulkan.o
IMGUI_VK = $(BUILD_DIR)/lib/libimgui_vk.a

# ---- per-variant gates (source-header presence) -----------------------------
TARGETS =
ifneq ($(wildcard $(GLES_HDR_DIR)/GLES2/gl2.h),)
TARGETS += $(IMGUI_GLES)
endif
ifneq ($(wildcard $(VK_HDR_DIR)/vulkan/vulkan.h),)
TARGETS += $(IMGUI_VK)
endif

# Header staging is shared by both variants.  Keep the stamp in the destination
# SDK: an ARCH-only source-tree stamp incorrectly suppresses installation for a
# different HW, or after that HW's build directory has been cleaned.
HDR_STAMP = $(BUILD_DIR)/include/imgui/.headers_staged

all: $(TARGETS)

$(IMGUI_GLES): $(GLES_OBJS) $(HDR_STAMP)
	@mkdir -p $(dir $@)
	rm -f $@
	$(AR) rcs $@ $(GLES_OBJS)

$(IMGUI_VK): $(VK_OBJS) $(HDR_STAMP)
	@mkdir -p $(dir $@)
	rm -f $@
	$(AR) rcs $@ $(VK_OBJS)

$(HDR_STAMP): $(wildcard *.h backends/*.h misc/cpp/*.h misc/freetype/*.h)
	mkdir -p $(BUILD_DIR)/include/imgui/backends
	cp -r *.h $(BUILD_DIR)/include/imgui/
	cp -r backends/*.h $(BUILD_DIR)/include/imgui/backends/
	mkdir -p $(BUILD_DIR)/include/imgui/misc/cpp
	cp -r misc/cpp/*.h $(BUILD_DIR)/include/imgui/misc/cpp/
	mkdir -p $(BUILD_DIR)/include/imgui/misc/freetype
	cp -r misc/freetype/*.h $(BUILD_DIR)/include/imgui/misc/freetype/
	@mkdir -p $(dir $@)
	@touch $@

# Compile C++ files (per-variant object trees)
$(ARCH)/gles/%.o: %.cpp
	@mkdir -p $(dir $@)
	$(CXX) $(GLES_CXXFLAGS) -c $< -o $@

$(ARCH)/gles/backends/%.o: backends/%.cpp
	@mkdir -p $(dir $@)
	$(CXX) $(GLES_CXXFLAGS) -c $< -o $@

$(ARCH)/vk/%.o: %.cpp
	@mkdir -p $(dir $@)
	$(CXX) $(VK_CXXFLAGS) -c $< -o $@

$(ARCH)/vk/backends/%.o: backends/%.cpp
	@mkdir -p $(dir $@)
	$(CXX) $(VK_CXXFLAGS) -c $< -o $@

.PHONY: all clean

clean:
	rm -rf $(ALL_ARCH_DIRS)
	rm -f $(IMGUI_GLES) $(IMGUI_VK)
