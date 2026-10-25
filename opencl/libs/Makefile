
# clclient builds the private GPU IPC transport (libclclient.a) and opencl builds
# libOpenCL.a + stages the public CL/*.h headers and archive into the shared
# build_$(ARCH)/$(HW) tree.  clclient is listed FIRST because opencl's GPU build
# ADDLIB-merges libclclient.a (plus the on-device mesa compiler archives opengl
# stages) into libOpenCL.a -- exactly as opengl folds portablegl + pglcompiler
# into libGLESv2.a.  On a CPU-only target clclient is a no-op and libOpenCL.a is
# the Phase-1 CPU library, so an app still links only -lOpenCL either way.
DIRS = clclient opencl

.PHONY: all clean

all:
	@for dir in $(DIRS); do \
		$(MAKE) -C $$dir || exit 1; \
	done

clean:
	@for dir in $(DIRS); do \
		if [ -d $$dir ]; then $(MAKE) -C $$dir clean; fi; \
	done
