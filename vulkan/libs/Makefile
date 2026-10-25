
# vkclient builds the private GPU IPC transport (libvkclient.a) and vulkan builds
# libvulkan.a + stages the public vulkan/*.h headers and archive into the shared
# build_$(ARCH)/$(HW) tree.  vkclient is listed FIRST because vulkan's GPU build
# ADDLIB-merges libvkclient.a (plus the on-device mesa compiler archives opengl
# stages) into libvulkan.a -- exactly as libs/opencl folds clclient into
# libOpenCL.a.  On a CPU-only target vkclient is a no-op and libvulkan.a is the
# software-device library, so an app still links only -lvulkan either way.
DIRS = vkclient vulkan

.PHONY: all clean

all:
	@for dir in $(DIRS); do \
		$(MAKE) -C $$dir || exit 1; \
	done

clean:
	@for dir in $(DIRS); do \
		if [ -d $$dir ]; then $(MAKE) -C $$dir clean; fi; \
	done
