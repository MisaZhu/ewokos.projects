
# OpenCL 1.2 /bin commands.  These are console utilities installed into
# rootfs/bin (shell-invoked commands), NOT GUI apps under rootfs/apps.  Each
# links only -lOpenCL (+ the system libs); the OpenCL-C front-end compiler and
# the CPU backend are folded into libOpenCL.a and invisible here.
#   clinfo      - query the platform/device registry
#   vector_add  - c = a + b      (1-D NDRange smoke test)
#   saxpy       - y = a*x + y    (scalar arg + float4 vload/vstore + fma)
#   matmul      - C = A * B      (2-D NDRange + indexed loop)
#   cltest      - CPU-vs-GPU benchmark: runs saxpy/matmul/mathmix on every
#                 registered device, verifies and compares launch times
DIRS := clinfo vector_add saxpy matmul cltest

all: $(DIRS)

$(DIRS):
	@$(MAKE) -C $@

.PHONY: all clean $(DIRS)

clean:
	@for dir in $(DIRS); do \
		$(MAKE) -C $$dir clean; \
	done
