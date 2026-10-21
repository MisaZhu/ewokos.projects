
DIRS := 3ddemo Fireworks matrix gears teapot imgui_demo glmatrix waterdrop engine wave ocean

all: $(DIRS)

$(DIRS):
	@$(MAKE) -C $@

.PHONY: all clean $(DIRS)

clean:
	@for dir in $(DIRS); do \
		$(MAKE) -C $$dir clean; \
	done
