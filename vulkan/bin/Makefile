
# Vulkan 1.3 /bin + /apps commands.  Each links only -lvulkan (+ the system
# libs); the SPIR-V front-end and the software device are folded into
# libvulkan.a and invisible here.
#   vkinfo    - query the instance / physical-device / device registry (console)
#   vktest    - compute correctness/performance + Vulkan API coverage (console)
DIRS := vkinfo vktest

all: $(DIRS)

$(DIRS):
	@$(MAKE) -C $@

.PHONY: all clean $(DIRS)

clean:
	@for dir in $(DIRS); do \
		$(MAKE) -C $$dir clean; \
	done
