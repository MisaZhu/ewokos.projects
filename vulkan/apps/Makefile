
# Vulkan 1.3 /apps GUI demos.  Like the /bin commands each links only -lvulkan
# (+ the system windowing libs); the SPIR-V front-end and the software device are
# folded into libvulkan.a and invisible here.
#   vkteapot - the Utah teapot: tessellated Bezier patches, CPU Blinn-Phong
#              lighting, mat4-UBO passthrough VS + depth test -> present (GUI app)
#   vkmatrix - "Inside the Matrix" (Shadertoy 4t3BWl): CPU-port digital rain
#              uploaded as a texture, fullscreen-quad VS/FS -> present (GUI app)
#   vkfonts  - "Better Text in Shaders" (Shadertoy 43t3WX): verbatim Common/Image
#              tabs run as the SPIR-V fragment shader over a font-atlas texture,
#              fullscreen-triangle VS -> present (GUI app)
#   vkolddisp - "Improved 96-bit 8x12 Font" (Shadertoy lc3BRn): native font-engine
#              port uploads a coverage texture, verbatim Buffer B/Image tabs run
#              as the SPIR-V fragment shader, fullscreen-triangle VS -> present
DIRS := vkteapot vkmatrix vkfonts vkolddisp

all: $(DIRS)

$(DIRS):
	@$(MAKE) -C $@

.PHONY: all clean $(DIRS)

clean:
	@for dir in $(DIRS); do \
		$(MAKE) -C $$dir clean; \
	done
