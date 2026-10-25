
DIRS = macemu \
	nesemu \
	opengl \
	opencl \
	blender \
	doom \
	cards \
	mine \
	vulkan
#	previous video saver minivmac nx11

# qt needs the Qt tree that projects/qt/build.sh produces; if it is missing for
# the current ARCH/HW, qt's Makefile now runs build.sh automatically and then
# carries on, so no manual build.sh step is required.

all: $(DIRS)

# blender consumes the GLES/EGL/imgui headers and libEGL/libGLESv2/libimgui that
# opengl stages into the shared build_$(ARCH)/$(HW) include+lib dirs.  The DIRS
# below are independent .PHONY targets, so under the machine build's default
# parallel make (-jN, jobserver shared with sub-makes) blender would otherwise
# start compiling before opengl finishes staging and die with
# "EGL/egl.h: No such file" / "imgui/imgui.h: No such file".  Order it after
# opengl (same idiom as opengl/Makefile's "apps: libs").
blender: opengl

# opencl's Phase-2 GPU front-end compiles kernels through the pglcompiler that
# opengl stages into build_$(ARCH)/$(HW); the CPU-only Phase-1 build does not
# need it, but order opencl after opengl anyway so the parallel machine build
# always finds pglcompiler staged before opencl links (same idiom as blender).
opencl: opengl

# vulkan's GPU front-end compiles SPIR-V->GLSL through the pglcompiler that
# opengl stages into build_$(ARCH)/$(HW); the software-device build does not need
# it, but order vulkan after opengl anyway so the parallel machine build always
# finds pglcompiler staged before vulkan links (same idiom as opencl).
vulkan: opengl

$(DIRS):
	@$(MAKE) -C $@

.PHONY: all clean $(DIRS)

clean:	
	@for dir in $(DIRS); do \
		$(MAKE) -C $$dir clean; \
	done
