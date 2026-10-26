
# gles builds libGLESv2.a/libEGL.a and now OWNS portablegl and pglcompiler
# (both moved under libs/gles/): its Makefile builds them as private
# intermediates and merges them into libGLESv2.a, so an app links only
# -lGLESv2 -lEGL and never sees portablegl or the on-device mesa GLSL->QPU
# compiler.  gles orders the sub-builds itself (pglcompiler -> portablegl ->
# merge), so neither appears here.
#
# glcommon and demos are enabled: glcommon is fully migrated to GLES2, and
# demos builds only the ported demos (balls.c, grass.cpp) -- its Makefile lists
# the still-unported shadertoy/sphere/cubemap as a migration backlog.
#
# imgui moved out of this tree to projects/imgui: it now builds two archives
# (libimgui_gles.a on the GLES2 backend, libimgui_vk.a on the Vulkan backend)
# and is ordered from projects/Makefile after opengl (GLES2 headers) and
# vulkan (vulkan headers).
DIRS = gles glcommon demos
# clean still sweeps every dir so stale objects never linger.
# gles clean cascades into portablegl + pglcompiler.

.PHONY: all clean

all: 
	@for dir in $(DIRS); do \
		$(MAKE) -C $$dir || exit 1; \
	done

clean: 
	@for dir in $(DIRS); do \
		if [ -d $$dir ]; then $(MAKE) -C $$dir clean; fi; \
	done
