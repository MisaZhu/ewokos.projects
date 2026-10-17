
# gles builds libGLESv2.a/libEGL.a and now OWNS portablegl and v3dcompiler
# (both moved under libs/gles/): its Makefile builds them as private
# intermediates and merges them into libGLESv2.a, so an app links only
# -lGLESv2 -lEGL and never sees portablegl or the on-device mesa GLSL->QPU
# compiler.  gles orders the sub-builds itself (v3dcompiler -> portablegl ->
# merge), so neither appears here.
#
# glcommon/demos/imgui are temporarily DISABLED: they still call portablegl's
# old gl* entry points, which were renamed to pgl_* to free the gl* namespace
# for the new libGLESv2/libEGL front end (libs/gles). They are re-enabled here
# as each is migrated to the standard GLES2+EGL API.
DIRS = gles glcommon
# clean still sweeps the disabled dirs so their stale objects never linger.
# gles clean cascades into portablegl + v3dcompiler.
CLEAN_DIRS = $(DIRS) demos imgui

.PHONY: all clean

all: 
	@for dir in $(DIRS); do \
		$(MAKE) -C $$dir || exit 1; \
	done

clean: 
	@for dir in $(CLEAN_DIRS); do \
		if [ -d $$dir ]; then $(MAKE) -C $$dir clean; fi; \
	done
