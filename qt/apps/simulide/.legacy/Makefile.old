#
# simulide - a port of https://github.com/SimulIDE/SimulIDE_SF (GPL-3.0),
# rebuilt on plain QtWidgets.  See mainwindow.h for why it is a rewrite rather
# than a source-level port: upstream needs Qt5Xml, Qt5Svg, Qt5Script,
# Qt5Concurrent, Qt5SerialPort and Qt5Multimedia, of which this Qt build has
# none, plus libelf and the vendored gpsim and simavr trees.
#
# The source is split the way upstream's src/ is, so a change can be located by
# the same reasoning in both trees:
#
#   simulator/   the MNA engine - e-node/e-pin/e-source, circmatrix, the step
#                loop.  Upstream: src/simulator/.
#   circuit/     the sheet - Component/Pin/Connector on a QGraphicsScene, and
#                the .simu reader and writer.  Upstream: src/gui/circuitwidget/.
#   components/  the component library, one file per upstream category
#                directory.  Upstream: src/gui/circuitwidget/components/.
#   avr/         the AVR core and its Intel HEX loader, standing in for
#                upstream's vendored simavr and gpsim.
#   gui/         the window around the sheet.  Upstream: src/gui/.
#
# Hand-written widgets only: no .ui and no .qrc, so this needs just moc.
#
# Standalone: builds against the SDK that `make sdk plugin` in projects/qt
# installed (see ../../qt.mk for the layout).
#
include ../../qt.mk

APP = simulide
BIN = $(SDK_DIR)/rootfs/apps/$(APP)/$(APP)
GEN = gen/$(ARCH)

SIMU = simulator
CIRC = circuit
COMP = components
AVRD = avr
GUI  = gui

CXXFLAGS += -I. -I$(SIMU) -I$(CIRC) -I$(COMP) -I$(AVRD) -I$(GUI) -I$(GEN)

# Upstream names its version into the binary from the .pro; there is no .pro
# here, so it is a define the About box and the .simu writer both read.
SIMU_DEFS = -DSIMULIDE_VERSION='"0.3.12-ewok"'

# Where the shipped examples and MCU data land in the rootfs - the same shape
# qterminal uses for its kb-layouts and color-schemes.
SIMU_DEFS += -DSIMULIDE_DATA_DIR='"/apps/simulide/res/data"'
SIMU_DEFS += -DSIMULIDE_EXAMPLES_DIR='"/apps/simulide/res/examples"'

CXXFLAGS += $(SIMU_DEFS)

# ---- simulator: the engine ---------------------------------------------------

SIMU_OBJS = $(patsubst %,$(ARCH)/$(SIMU)/%.o, \
	e-element e-pin e-node e-source \
	matrixsolver circmatrix simulator)

SIMU_MOC_OBJS = $(patsubst %,$(GEN)/$(SIMU)/moc_%.o, \
	simulator)

# ---- circuit: the sheet ------------------------------------------------------

CIRC_OBJS = $(patsubst %,$(ARCH)/$(CIRC)/%.o, \
	itemlibrary component pin connector node \
	circuit circuitview circuitwidget chip)

CIRC_MOC_OBJS = $(patsubst %,$(GEN)/$(CIRC)/moc_%.o, \
	component pin connector node circuit circuitview circuitwidget chip)

# ---- components: the library -------------------------------------------------

COMP_OBJS = $(patsubst %,$(ARCH)/$(COMP)/%.o, \
	twopin \
	passive sources switches active logic outputs meters other mcu)

COMP_MOC_OBJS = $(patsubst %,$(GEN)/$(COMP)/moc_%.o, \
	twopin \
	passive sources switches active logic outputs meters other mcu)

# ---- avr: the core -----------------------------------------------------------

AVR_OBJS = $(patsubst %,$(ARCH)/$(AVRD)/%.o, \
	avr_core avr_ihex avr_periph avr_devs)

AVR_MOC_OBJS =

# ---- gui: the window ---------------------------------------------------------

GUI_OBJS = $(patsubst %,$(ARCH)/$(GUI)/%.o, \
	mainwindow componentselector propertieswidget oscopewidget plotterwidget)

GUI_MOC_OBJS = $(patsubst %,$(GEN)/$(GUI)/moc_%.o, \
	mainwindow componentselector propertieswidget oscopewidget plotterwidget)

APP_OBJS = $(ARCH)/main.o
APP_MOC_OBJS =

OBJS = \
	$(SIMU_OBJS) $(SIMU_MOC_OBJS) \
	$(CIRC_OBJS) $(CIRC_MOC_OBJS) \
	$(COMP_OBJS) $(COMP_MOC_OBJS) \
	$(AVR_OBJS) $(AVR_MOC_OBJS) \
	$(GUI_OBJS) $(GUI_MOC_OBJS) \
	$(APP_OBJS) $(APP_MOC_OBJS)

MOC_CPPS = \
	$(SIMU_MOC_OBJS:.o=.cpp) $(CIRC_MOC_OBJS:.o=.cpp) \
	$(COMP_MOC_OBJS:.o=.cpp) $(AVR_MOC_OBJS:.o=.cpp) \
	$(GUI_MOC_OBJS:.o=.cpp) $(APP_MOC_OBJS:.o=.cpp)

# The moc_*.cpp are chain intermediates (header -> .cpp -> .o); keep them on
# disk or make reruns the whole moc+compile+link chain every build.
.SECONDARY: $(MOC_CPPS)

# moc is handed the defines and the include path but not the compiler flags,
# and is run from inside the source dir so the generated #include names the
# bare header - which the -I list above then resolves the same way g++ does.
# moc also gets SIMU_DEFS: a Q_OBJECT class guarded on one of them would
# otherwise generate a meta-object that disagrees with the compiled one.
define MOC_RULE
$(GEN)/$(1)/moc_%.cpp: $(1)/%.h
	@mkdir -p $$(dir $$@)
	cd $(1) && $(QT_MOC) $(QT_DEFS) $(SIMU_DEFS) $(QT_INCS) $$(notdir $$<) -o $(CURDIR)/$$@

$(GEN)/$(1)/%.o: $(GEN)/$(1)/%.cpp
	@mkdir -p $$(dir $$@)
	$(CXX) $(CXXFLAGS) -c $$< -o $$@

endef

$(eval $(call MOC_RULE,$(SIMU)))
$(eval $(call MOC_RULE,$(CIRC)))
$(eval $(call MOC_RULE,$(COMP)))
$(eval $(call MOC_RULE,$(AVRD)))
$(eval $(call MOC_RULE,$(GUI)))

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
