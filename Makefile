
DIRS = macemu \
	nesemu \
	soft3d \
	blender \
	doom \
	cards \
	mine
#	previous video saver minivmac nx11

# qt needs the Qt tree that projects/qt/build.sh produces; if it is missing for
# the current ARCH/HW, qt's Makefile now runs build.sh automatically and then
# carries on, so no manual build.sh step is required.

all: $(DIRS)

# blender consumes the GLES/EGL/imgui headers and libEGL/libGLESv2/libimgui that
# soft3d stages into the shared build_$(ARCH)/$(HW) include+lib dirs.  The DIRS
# below are independent .PHONY targets, so under the machine build's default
# parallel make (-jN, jobserver shared with sub-makes) blender would otherwise
# start compiling before soft3d finishes staging and die with
# "EGL/egl.h: No such file" / "imgui/imgui.h: No such file".  Order it after
# soft3d (same idiom as soft3d/Makefile's "apps: libs").
blender: soft3d

$(DIRS):
	@$(MAKE) -C $@

.PHONY: all clean $(DIRS)

clean:	
	@for dir in $(DIRS); do \
		$(MAKE) -C $$dir clean; \
	done
