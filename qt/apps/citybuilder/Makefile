#
# citybuilder - a port of https://github.com/Vinorcola/CityBuilderEngine
# (GPL-3.0), a Qt 5.15 / C++14 2D city-builder and a reimplementation of
# Zeus - Master of Olympus.  Unlike its sibling apps here this is a genuine
# source-level port: upstream only needs QtCore/QtGui/QtWidgets, all of which
# this Qt build has, so src/ is compiled largely as-is.  Two shims make it fit
# the EwokOS toolchain, which builds everything -fno-exceptions -fno-rtti:
#
#   yaml-cpp/   upstream depends on yaml-cpp, absent from the tree.  This is a
#               small no-exception stand-in covering exactly the YAML:: subset
#               the loader uses (see yaml-cpp/yaml.h).  It is compiled in via
#               yaml-cpp/yaml.cpp, on the -I. path so <yaml-cpp/yaml.h> from
#               src/ resolves.
#   src/exceptions/  the Exception base logs and abort()s instead of throwing;
#               throw sites became (void) casts and the one recoverable
#               try/catch (Map.cpp) became a nullptr sentinel.  See the port
#               notes in those files.
#
# Standalone: builds against the SDK that `make sdk plugin` in projects/qt
# installed (see ../../qt.mk for the layout).
#
include ../../qt.mk

APP = citybuilder
BIN = $(SDK_DIR)/rootfs/apps/$(APP)/$(APP)
GEN = gen/$(ARCH)
RES_DST = $(SDK_DIR)/rootfs/apps/$(APP)/res

# -I. puts the app root on the path: src/ headers are included root-relative
# ("src/engine/map/Map.hpp") and the shim is included as <yaml-cpp/yaml.h>, so
# both resolve from here.  Same-directory includes ("Map.hpp") are found by the
# usual rule relative to the including file.
CXXFLAGS += -I. -I$(GEN)

# Where res/ lands in the rootfs (copied by the $(BIN) rule below); main.cpp
# reads the game config, maps and images from here rather than the upstream
# working-copy relative "assets/zeus".
CXXFLAGS += -DCBE_ASSETS_DIR='"/apps/$(APP)/res/zeus"'

# 87 translation units across a deep tree (src/**) plus the yaml shim.  The
# sibling apps hardcode a flat list; here that would be 87 hand-maintained
# paths, so the sources are discovered instead.  sort keeps the list stable
# across machines (find's order is filesystem-dependent), which matters for
# reproducible builds.
SRC_CPPS = $(shell find src yaml-cpp -name '*.cpp' | sort)
APP_OBJS = $(patsubst %.cpp,$(ARCH)/%.o,$(SRC_CPPS))

# Every header carrying Q_OBJECT needs a moc unit.  All nine are .hpp and none
# is a .cpp (which would instead #include a .moc), so one header->moc->object
# chain covers them.  MOC_OBJS keeps the source path under the moc_ prefix,
# e.g. src/ui/MainWindow.hpp -> gen/$(ARCH)/moc_src/ui/MainWindow.o.
MOC_HDRS = $(shell grep -rl Q_OBJECT src --include='*.hpp' | sort)
MOC_OBJS = $(patsubst %.hpp,$(GEN)/moc_%.o,$(MOC_HDRS))

OBJS = $(APP_OBJS) $(MOC_OBJS)

# The moc_*.cpp are chain intermediates (header -> .cpp -> .o); keep them on
# disk or make reruns the whole moc+compile+link chain every build.
.SECONDARY: $(MOC_OBJS:.o=.cpp)

# %.hpp (not %.h as in the sibling apps): CBE's headers use the .hpp suffix.
# The mkdir is $(dir $@), not $(GEN), because the target mirrors the nested
# source path (gen/$(ARCH)/moc_src/ui/...).
$(GEN)/moc_%.cpp: %.hpp
	@mkdir -p $(dir $@)
	$(QT_MOC) $(QT_DEFS) $(QT_INCS) $< -o $@

$(GEN)/%.o: $(GEN)/%.cpp
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) -c $< -o $@

ifeq ($(QT_SDK_MISSING),yes)
all:
	@echo "note: $(APP) skipped - Qt SDK not installed ($(QT_MOC) missing)."
	@echo "      Run make in projects/qt first."
else
all: $(BIN) $(RES_DST)/icon.png
endif

# The launcher (xlauncher getIconFname) looks up /apps/<app>/res/icon.png, so the
# icon must land in the installed res/, not just the source tree.  The $(BIN)
# rule's `cp -r res` already carries it on every relink, but that misses the
# common case of editing the icon while the binary is up to date; this target
# installs it incrementally on its own.  Kept separate from the bulk `cp -r res`
# so touching the icon never recopies the ~2400-file zeus/ asset tree.
$(RES_DST)/icon.png: res/icon.png
	@mkdir -p $(RES_DST)
	cp -f $< $@

$(BIN): $(OBJS)
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) $(OBJS) -o $@ $(QT_LDFLAGS)
	cp -r res $(dir $@)

-include $(OBJS:.o=.d)

clean:
	rm -rf $(ARCH) gen

.PHONY: all clean
