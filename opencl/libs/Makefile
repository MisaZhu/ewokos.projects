
# opencl builds libOpenCL.a and stages the public CL/*.h headers + archive into
# the shared build_$(ARCH)/$(HW) tree.  (Phase 2 adds clclient as a private
# intermediate folded into libOpenCL.a, exactly as opengl folds portablegl into
# libGLESv2.a; for now the single CPU-complete library is the only member.)
DIRS = opencl

.PHONY: all clean

all:
	@for dir in $(DIRS); do \
		$(MAKE) -C $$dir || exit 1; \
	done

clean:
	@for dir in $(DIRS); do \
		if [ -d $$dir ]; then $(MAKE) -C $$dir clean; fi; \
	done
