PROJS_ROOT_DIR=../../..
include $(PROJS_ROOT_DIR)/make.inc

TASK_OBJS = $(ARCH)/gles2_conformance.o
EXTRA_LIBS_DIR = $(EWOKOS_SYS_DIR)/build_$(ARCH)/$(HW)

CFLAGS += -I.. -I $(EXTRA_LIBS_DIR)/include -D__ewokos__
LDFLAGS += -L $(EXTRA_LIBS_DIR)/lib
GLES_LIBS = $(EXTRA_LIBS_DIR)/lib/libEGL.a \
	$(EXTRA_LIBS_DIR)/lib/libGLESv2.a

HOST_CC ?= cc
HOST_CFLAGS ?= -std=c11 -O1 -ffp-contract=off -fno-strict-aliasing
HOST_OUT ?= .host/texture-test
GLSL_DIR = ../../libs/gles/src/glsl
GLSL_SOURCES = $(addprefix $(GLSL_DIR)/,glsl_lex.c glsl_parse.c glsl_program.c glsl_exec.c)
HOST_SOURCES = tests/texture_test.c ../../libs/gles/portablegl/src/portablegl.c $(GLSL_SOURCES)
HOST_HEADERS = $(wildcard $(GLSL_DIR)/*.h $(GLSL_DIR)/*.inc) \
	../../libs/gles/portablegl/include/portablegl/portablegl.h

TASK = gles2_conformance
TASK_DIR = $(EWOKOS_SYS_DIR)/build_$(ARCH)/$(HW)/rootfs/apps/$(TASK)

all: $(TASK)

$(TASK): $(TASK_OBJS) $(GLES_LIBS) \
		$(SDK_DIR)/lib/libewoksys.a \
		$(SDK_DIR)/lib/libx.a \
		$(SDK_DIR)/lib/libgraph.a \
		$(SDK_DIR)/lib/libopenlibm.a
	mkdir -p $(TASK_DIR)
	$(LD) -Ttext=100 $(TASK_OBJS) $(GLES_LIBS) -o $(TASK_DIR)/$(TASK) \
		$(LDFLAGS) $(EWOK_LIB_X) $(EWOK_LIB_GRAPH) -lopenlibm $(EWOK_LIBC)

.PHONY: host-test
host-test: $(HOST_OUT)
	$(HOST_OUT)

$(HOST_OUT): $(HOST_SOURCES) $(HOST_HEADERS)
	@mkdir -p $(dir $@)
	$(HOST_CC) $(HOST_CFLAGS) $(HOST_SOURCES) -lm -o $@

clean:
	rm -rf $(ALL_ARCH_DIRS) .host
	rm -f *.o
