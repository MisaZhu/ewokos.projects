
# gles builds libGLESv2.a/libEGL.a and now OWNS portablegl and v3dcompiler
# (both moved under libs/gles/): its Makefile builds them as private
# intermediates and merges them into libGLESv2.a, so an app links only
# -lGLESv2 -lEGL and never sees portablegl or the on-device mesa GLSL->QPU
# compiler.  gles orders the sub-builds itself (v3dcompiler -> portablegl ->
# merge), so neither appears here.
#
# glcommon and demos are enabled: glcommon is fully migrated to GLES2, and
# demos builds only the ported demos (balls.c, grass.cpp) -- its Makefile lists
# the still-unported shadertoy/sphere/cubemap as a migration backlog.
#
# imgui is migrated to GLES2+EGL: its single renderer backend
# (backends/imgui_impl_gles2.cpp) draws ImGui's VBO/element-stream through the
# standard libGLESv2 GLSL ES 1.00 front end -- no portablegl, no VAOs, no
# glPolygonMode.  It builds after gles so <GLES2/gl2.h> is installed.
DIRS = gles glcommon demos imgui
# clean still sweeps every dir so stale objects never linger.
# gles clean cascades into portablegl + v3dcompiler.

.PHONY: all clean

all: 
	@for dir in $(DIRS); do \
		$(MAKE) -C $$dir || exit 1; \
	done

clean: 
	@for dir in $(DIRS); do \
		if [ -d $$dir ]; then $(MAKE) -C $$dir clean; fi; \
	done
