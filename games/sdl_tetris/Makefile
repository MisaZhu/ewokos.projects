PROJS_ROOT_DIR=../..
include $(PROJS_ROOT_DIR)/make.inc

LDFLAGS += -lSDL2_image -lSDL2_mixer -lSDL2_gfx -lSDL2_ttf -lSDL2 -lfreetype -lpng -lvorbis -logg -lz -lopenlibm

TASK_OBJS = $(ARCH)/graphics.o $(ARCH)/input.o $(ARCH)/game.o $(ARCH)/main.o

TASK = tetris
BUILD_DIR = $(SDK_DIR)/rootfs/apps/games/$(TASK)

all: $(TASK)

$(TASK): $(TASK_OBJS) \
		$(SDK_DIR)/lib/libewoksys.a \
		$(SDK_DIR)/lib/libSDL2.a \
		$(SDK_DIR)/lib/libSDL2_image.a \
		$(SDK_DIR)/lib/libSDL2_mixer.a \
		$(SDK_DIR)/lib/libSDL2_ttf.a \
		$(SDK_DIR)/lib/libfreetype.a \
		$(SDK_DIR)/lib/libpng.a \
		$(SDK_DIR)/lib/libewokstl.a \
		$(SDK_DIR)/lib/libx++.a \
		$(SDK_DIR)/lib/libx.a \
		$(SDK_DIR)/lib/libfont.a \
		$(SDK_DIR)/lib/libgraph.a \
		$(SDK_DIR)/lib/libwidget++.a \
		$(SDK_DIR)/lib/libcxx.a \
		$(SDK_DIR)/lib/libSDL2_gfx.a \
		$(SDK_DIR)/lib/libvorbis.a \
		$(SDK_DIR)/lib/libogg.a
	mkdir -p $(BUILD_DIR)
	$(LD) -Ttext=100 $(TASK_OBJS) -o $(BUILD_DIR)/$(TASK) $(LDFLAGS) $(EWOK_LIB_X) $(EWOK_LIB_GRAPH) $(EWOK_LIBC) -lcxx
	if [ -d res ]; then cp -r res $(BUILD_DIR); fi

clean:
	rm -f $(TASK_OBJS)