
# Vulkan 1.3 /apps GUI demos.  Like the /bin commands each links only -lvulkan
# (+ the system windowing libs); the SPIR-V front-end and the software device are
# folded into libvulkan.a and invisible here.
#   vkteapot - the Utah teapot: tessellated Bezier patches, CPU Blinn-Phong
#              lighting, mat4-UBO passthrough VS + depth test -> present (GUI app)
DIRS := vkteapot

all: $(DIRS)

$(DIRS):
	@$(MAKE) -C $@

.PHONY: all clean $(DIRS)

clean:
	@for dir in $(DIRS); do \
		$(MAKE) -C $$dir clean; \
	done
