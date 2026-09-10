
DIRS = macemu \
	nesemu \
	soft3d \
	doom \
	cards \
	mine \
	browser \
	qt
#	previous video saver minivmac nx11

# qt needs the Qt tree that projects/qt/build.sh produces; if it is missing for
# the current ARCH/HW, qt's Makefile now runs build.sh automatically and then
# carries on, so no manual build.sh step is required.

all: $(DIRS)

$(DIRS):
	@$(MAKE) -C $@

.PHONY: all clean $(DIRS)

clean:	
	@for dir in $(DIRS); do \
		$(MAKE) -C $$dir clean; \
	done
