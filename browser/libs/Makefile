# libwebp first: widget++ resolves <webp.h> from $(BUILD_DIR)/include, which
# libwebp is what populates.
DIRS = libwebp litehtml widget++

all: basic_libs
	@echo "all done."

basic_libs:
	@for dir in $(DIRS); do \
		$(MAKE) -C $$dir || exit 1; \
	done

clean:
	@for dir in $(DIRS); do \
		$(MAKE) -C $$dir clean; \
	done
