#
# qucs-s - a port of https://github.com/ra3xdh/qucs_s (GPL-2.0), rebuilt on
# plain QtWidgets - see mainwindow.h for why it is a rewrite rather than a
# source-level port (upstream needs Qt6, QtCharts, QtSvg and OpenGL, and runs
# its analysis in an external SPICE kernel through QProcess, none of which this
# Qt build has).  Hand-written widgets only: no .ui and no .qrc, so this needs
# just moc.
#
# Standalone: builds against the SDK that `make sdk plugin` in projects/qt
# installed (see ../../qt.mk for the layout).
#
include ../../qt.mk

APP = qucs-s
BIN = $(SDK_DIR)/rootfs/apps/$(APP)/$(APP)
GEN = gen/$(ARCH)

CXXFLAGS += -I. -I$(GEN)

# Where the shipped examples land in the rootfs; mainwindow.cpp lists whatever
# it finds there under File > Examples.
CXXFLAGS += -DQUCS_EXAMPLES_DIR='"/apps/qucs-s/res/examples"'

# The dependency order of the sources, which is also the reading order:
# misc/components/schematic are the document, netlist/simulator the analysis,
# diagram/view/compdialog the widgets and mainwindow the shell around them.
APP_OBJS = $(patsubst %,$(ARCH)/%.o, \
	main mainwindow view diagram compdialog schematic components netlist \
	simulator misc)

MOC_OBJS = $(patsubst %,$(GEN)/moc_%.o, \
	mainwindow view compdialog schematic)

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
