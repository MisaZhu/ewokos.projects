
DIRS := 3ddemo Fireworks matrix teapot imgui_demo glmatrix ocean grass rainwin colorglass pixar tuner macgl bricks cnn gles2_conformance

all: $(DIRS)

$(DIRS):
	@$(MAKE) -C $@

.PHONY: all clean $(DIRS)

clean:
	@for dir in $(DIRS); do \
		$(MAKE) -C $$dir clean; \
	done
