
DIRS = macemu \
	nesemu \
	soft3d \
	doom \
	cards \
	mine \
	browser \
	nx11 \
	qt
#	previous video saver minivmac nx11 fltk

# qt needs the Qt tree that projects/qt/build.sh produces; without it the
# sub-make prints a note and skips, so every other directory still builds.

all: $(DIRS)

$(DIRS):
	@$(MAKE) -C $@

.PHONY: all clean $(DIRS)

clean:	
	@for dir in $(DIRS); do \
		$(MAKE) -C $$dir clean; \
	done
