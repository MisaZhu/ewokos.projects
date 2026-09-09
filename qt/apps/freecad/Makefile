#
# freecad - a port of https://github.com/FreeCAD/FreeCAD.git (LGPL-2.1),
# rebuilt on plain QtWidgets - see mainwindow.h for why it is a rewrite
# rather than a source-level port (upstream needs OpenCASCADE, Coin3D/OpenGL
# and embedded Python, and this Qt build has no OpenGL at all).  Hand-written
# widgets only: no .ui and no .qrc, so this needs just moc.
#
# Standalone: builds against the SDK that `make sdk plugin` in projects/qt
# installed (see ../../qt.mk for the layout).
#
include ../../qt.mk

APP = freecad
BIN = $(SDK_DIR)/rootfs/apps/$(APP)/$(APP)
GEN = gen/$(ARCH)

CXXFLAGS += -I. -I$(GEN)

APP_OBJS = $(patsubst %,$(ARCH)/%.o, \
	main mainwindow document viewport propertypanel)

MOC_OBJS = $(patsubst %,$(GEN)/moc_%.o, \
	mainwindow document viewport propertypanel)

OBJS = $(APP_OBJS) $(MOC_OBJS)

# The moc_*.cpp are chain intermediates (header -> .cpp -> .o); keep them on
# disk or make reruns the whole moc+compile+link chain every build.
.SECONDARY: $(MOC_OBJS:.o=.cpp)

$(GEN)/moc_%.cpp: %.h
	@mkdir -p $(GEN)
	$(QT_MOC) $(QT_DEFS) $(QT_INCS) $< -o $@

$(GEN)/%.o: $(GEN)/%.cpp
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) -c $< -o $@

ifeq ($(QT_SDK_MISSING),yes)
all:
	@echo "note: $(APP) skipped - Qt SDK not installed ($(QT_MOC) missing)."
	@echo "      Run make in projects/qt first."
else
all: $(BIN)
endif

$(BIN): $(OBJS)
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) $(OBJS) -o $@ $(QT_LDFLAGS)
	cp -r res $(dir $@)

-include $(OBJS:.o=.d)

clean:
	rm -rf $(ARCH) gen

.PHONY: all clean
