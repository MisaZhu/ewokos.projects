
DIRS = libs bin

all: $(DIRS)

# the /bin commands link libOpenCL.a and include <CL/cl.h>, both staged into the
# shared build_$(ARCH)/$(HW) tree by libs; order bin after libs (same idiom as
# projects/opengl/Makefile).
bin: libs

$(DIRS):
	@$(MAKE) -C $@

.PHONY: all clean $(DIRS)

clean:
	@for dir in $(DIRS); do \
		$(MAKE) -C $$dir clean; \
	done
