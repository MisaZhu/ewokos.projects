
# v3dcompiler first: it builds libv3dcompiler.a (raspi5-only on-device mesa
# GLSL->QPU compiler) that portablegl's pgl_g3d backend links against.
DIRS = v3dcompiler portablegl glcommon demos # imgui

.PHONY: all clean

all: 
	@for dir in $(DIRS); do \
		$(MAKE) -C $$dir || exit 1; \
	done

clean: 
	@for dir in $(DIRS); do \
		$(MAKE) -C $$dir clean; \
	done
