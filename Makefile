
DIRS = macemu \
	nesemu \
	soft3d \
	doom \
	cards \
	mine \
	browser
#	previous video saver minivmac nx11 fltk

all: $(DIRS)

$(DIRS):
	@$(MAKE) -C $@

.PHONY: all clean $(DIRS)

clean:	
	@for dir in $(DIRS); do \
		$(MAKE) -C $$dir clean; \
	done
