
DIRS = portablegl glcommon demos # imgui

.PHONY: all clean

all: 
	@for dir in $(DIRS); do \
		$(MAKE) -C $$dir || exit 1; \
	done

clean: 
	@for dir in $(DIRS); do \
		$(MAKE) -C $$dir clean; \
	done
