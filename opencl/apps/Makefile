
# CPU-complete OpenCL 1.2 demos.  Each is a console app that links only
# -lOpenCL (+ the system libs); the front-end compiler and CPU backend are
# folded into libOpenCL.a and invisible to the app.  clinfo queries the
# platform/device registry; vector_add / saxpy / matmul self-check their results
# against a host reference and return non-zero on mismatch.
DIRS := clinfo vector_add saxpy matmul

all: $(DIRS)

$(DIRS):
	@$(MAKE) -C $@

.PHONY: all clean $(DIRS)

clean:
	@for dir in $(DIRS); do \
		$(MAKE) -C $$dir clean; \
	done
