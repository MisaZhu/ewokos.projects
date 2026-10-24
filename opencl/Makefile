
DIRS = libs apps

all: $(DIRS)

# apps link libOpenCL.a and include <CL/cl.h>, both staged into the shared
# build_$(ARCH)/$(HW) tree by libs; order apps after libs (same idiom as
# projects/opengl/Makefile).
apps: libs

$(DIRS):
	@$(MAKE) -C $@

.PHONY: all clean $(DIRS)

clean:
	@for dir in $(DIRS); do \
		$(MAKE) -C $$dir clean; \
	done
