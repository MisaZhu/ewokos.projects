#
# Qt 5.15 on EwokOS: the "ewokos" QPA platform plugin, and a Widgets demo that
# links it.
#
# This builds against the tree projects/qt/build.sh produced - configure plus
# make in build/qtbuild-$(ARCH)-$(HW), with the sources in build/qtbase-5.15.
# Run build.sh first; nothing here can work without it.
#
# The plugin is deliberately NOT a qmake project.  qmake would want to run
# against an installed Qt (a qt5 .pri, a module list, a working qmake for the
# target), and the value of building it by hand is that it lives in this repo,
# under version control, next to the mkspec and the patches it depends on - so
# changing it does not mean re-running configure or touching build/qtbase-5.15,
# which is an unpacked tarball.
#
# Three flags here are load-bearing and easy to lose:
#
#   -D__EWOKOS__      qsystemdetection.h keys Q_OS_EWOKOS and Q_OS_UNIX off this
#                     macro, because a bare -elf toolchain predefines no OS macro
#                     of its own.  Without it Qt compiles as "unknown platform":
#                     Q_OS_UNIX goes away, qcore_unix_p.h stops declaring
#                     qt_safe_poll, and this plugin fails on its first include.
#                     Qt's own objects got it from the mkspec's EWOK_CXXFLAGS.
#
#   -DQT_NO_DEBUG     Qt was configured -release.  Q_ASSERT, the layout of a few
#                     private classes and the inline/non-inline split of several
#                     QtCore functions all differ between the two, so the plugin
#                     and libQt5*.a have to agree.
#
#   -DQT_STATICPLUGIN only for the plugin's own objects.  It is what makes
#                     QT_MOC_EXPORT_PLUGIN emit the static form -
#                     qt_static_plugin_EwokosIntegrationPlugin() - instead of the
#                     extern "C" dllexport form a shared plugin needs.  The demo
#                     must NOT have it: that is the QT_PLUGIN side of the
#                     distinction, and the app is the importer.
#
PROJS_ROOT_DIR=..
include $(PROJS_ROOT_DIR)/make.inc

# make.inc/make.rule define no target of their own, so without this the first rule
# in the file becomes the default goal - and that is the $(QT_MOC) guard below,
# which make then reports as "up to date" and exits having built nothing.
.DEFAULT_GOAL := all

# ---- the Qt build ----------------------------------------------------------

QT_VER    = 5.15.19
QT_SRC    = $(EWOKOS_ROOT_DIR)/build/qtbase-5.15
QT_BUILD  = $(EWOKOS_ROOT_DIR)/build/qtbuild-$(ARCH)-$(HW)
QT_INC    = $(QT_BUILD)/include
QT_LIBDIR = $(QT_BUILD)/lib
QT_MOC    = $(QT_BUILD)/bin/moc

# qplatformdefs.h.  QtCore/private/qcore_unix_p.h includes it by that bare name,
# so the directory has to be on the path even though nothing here names it.  This
# is the copy build.sh put into the Qt tree rather than the one under
# projects/qt/mkspecs, because it in turn includes "../common/posix/..." and only
# Qt's own mkspecs directory has that.
QT_MKSPEC = $(QT_SRC)/mkspecs/ewokos-g++

QT_DEFS = \
	-D__EWOKOS__ \
	-DQT_NO_DEBUG \
	-DQT_CORE_LIB \
	-DQT_GUI_LIB \
	-DQT_WIDGETS_LIB \
	-DQT_FONTDATABASE_SUPPORT_LIB

# Four levels per module, and all four are needed:
#   include/                      <QtCore/qdebug.h>
#   include/QtCore/               <QtCore/qtcore-config.h> from qglobal.h
#   include/<Mod>/<ver>/          <QtCore/private/qtimerinfo_unix_p.h>
#   include/<Mod>/<ver>/<Mod>/    <qpa/qplatformwindow.h>
QT_INCS = \
	-I$(QT_MKSPEC) \
	-I$(QT_INC) \
	-I$(QT_INC)/QtCore \
	-I$(QT_INC)/QtCore/$(QT_VER) \
	-I$(QT_INC)/QtCore/$(QT_VER)/QtCore \
	-I$(QT_INC)/QtGui \
	-I$(QT_INC)/QtGui/$(QT_VER) \
	-I$(QT_INC)/QtGui/$(QT_VER)/QtGui \
	-I$(QT_INC)/QtWidgets \
	-I$(QT_INC)/QtWidgets/$(QT_VER) \
	-I$(QT_INC)/QtWidgets/$(QT_VER)/QtWidgets \
	-I$(QT_INC)/QtFontDatabaseSupport/$(QT_VER)

# -Wextra and -pedantic are dropped for the same reason the mkspec drops them:
# Qt 5.15's own private headers are not clean under them at this GCC version, and
# the noise buries the errors in the files that are actually ours.  -Wall stays.
CXXFLAGS := $(filter-out -Wextra -pedantic,$(CXXFLAGS))
CXXFLAGS += $(QT_DEFS) $(QT_INCS) -Wno-error -MMD -MP

# Guard, as a rule rather than $(error): qt is in projects/Makefile's DIRS, so a
# parse-time error here would stop `make clean` at the projects level on any tree
# that has not built Qt.  A missing prerequisite only fails the target that needs
# it, and says what to do about it.
$(QT_MOC):
	@echo "Qt has not been built: $(QT_MOC) does not exist." >&2
	@echo "Run projects/qt/build.sh first (it unpacks, patches and configures Qt)." >&2
	@false

# ---- generated sources ------------------------------------------------------
#
# Under gen/ rather than $(ARCH)/ so that make.rule's "$(ARCH)/%.o: %.cpp"
# pattern cannot also match them.  Two rules matching one target is resolved by
# which prerequisite exists, which works, but is a thing better not relied on.

GEN_DIR        = gen/$(ARCH)
MOC_PLUGIN_DIR = $(GEN_DIR)/plugin
MOC_DEMO_DIR   = $(GEN_DIR)/demo

PLUGIN_SRCS = \
	src/platform/ewokos/qewokoskeymap.cpp \
	src/platform/ewokos/qewokosscreen.cpp \
	src/platform/ewokos/qewokoswindow.cpp \
	src/platform/ewokos/qewokosbackingstore.cpp \
	src/platform/ewokos/qewokosfontdatabase.cpp \
	src/platform/ewokos/qewokosintegration.cpp \
	src/platform/ewokos/qewokoseventdispatcher.cpp \
	src/platform/ewokos/main.cpp

PLUGIN_OBJS  = $(patsubst %.cpp,$(ARCH)/%.o,$(PLUGIN_SRCS))
PLUGIN_OBJS += $(MOC_PLUGIN_DIR)/moc_qewokoseventdispatcher.o

PLUGIN_LIB = $(SDK_DIR)/lib/libewokosqpa.a
PLUGIN_HDR = $(SDK_DIR)/include/qt/ewokosqt.h

DEMO     = qtdemo
DEMO_DIR = $(SDK_DIR)/rootfs/apps/$(DEMO)
DEMO_BIN = $(DEMO_DIR)/$(DEMO)
DEMO_OBJ = $(ARCH)/demo/main.o

# moc is handed the defines and the include path but not the compiler flags: it
# parses the sources itself and only understands -D, -I and -f, so -nostdinc++
# and friends would be rejected rather than ignored.
$(MOC_PLUGIN_DIR)/moc_%.cpp: src/platform/ewokos/%.h
	@mkdir -p $(MOC_PLUGIN_DIR)
	$(QT_MOC) $(QT_DEFS) $(QT_INCS) $< -o $@

$(MOC_PLUGIN_DIR)/%.moc: src/platform/ewokos/%.cpp
	@mkdir -p $(MOC_PLUGIN_DIR)
	$(QT_MOC) $(QT_DEFS) $(QT_INCS) $< -o $@

$(MOC_DEMO_DIR)/%.moc: demo/%.cpp
	@mkdir -p $(MOC_DEMO_DIR)
	$(QT_MOC) $(QT_DEFS) $(QT_INCS) $< -o $@

$(MOC_PLUGIN_DIR)/%.o: $(MOC_PLUGIN_DIR)/%.cpp
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) -DQT_STATICPLUGIN -I$(MOC_PLUGIN_DIR) -c $< -o $@

# Both main.cpp files #include "main.moc", and a quoted include searches the
# including file's own directory first and then -I in order - so the two
# generated main.moc files have to be on separate paths, and only their own path
# on each side.  Same for QT_STATICPLUGIN, which is the plugin's and not the
# demo's.
$(PLUGIN_OBJS): CXXFLAGS += -DQT_STATICPLUGIN -I$(MOC_PLUGIN_DIR)
$(DEMO_OBJ):    CXXFLAGS += -I$(MOC_DEMO_DIR)

$(ARCH)/src/platform/ewokos/main.o: $(MOC_PLUGIN_DIR)/main.moc
$(ARCH)/src/platform/ewokos/qewokoseventdispatcher.o: $(MOC_PLUGIN_DIR)/moc_qewokoseventdispatcher.cpp
$(DEMO_OBJ): $(MOC_DEMO_DIR)/main.moc

# ---- the plugin -------------------------------------------------------------

# ar appends, so a stale archive would keep the previous build's objects next to
# the new ones and link whichever satisfied a symbol first.
$(PLUGIN_LIB): $(PLUGIN_OBJS)
	@mkdir -p $(dir $@)
	rm -f $@
	$(AR) cqs $@ $(PLUGIN_OBJS)

$(PLUGIN_HDR): include/qt/ewokosqt.h
	@mkdir -p $(dir $@)
	cp $< $@

# ---- the demo ---------------------------------------------------------------

QT_LIBS = \
	-lQt5Widgets -lQt5Gui \
	-lQt5FontDatabaseSupport -lQt5EventDispatcherSupport \
	-lQt5ThemeSupport -lQt5AccessibilitySupport -lQt5ServiceSupport \
	-lQt5DeviceDiscoverySupport -lQt5EdidSupport -lQt5FbSupport \
	-lQt5Core -lqtharfbuzz -lqtlibpng -lqtlibjpeg -lqtpcre2

# One group rather than a careful order: QtCore, QtGui, libx, libewoksys and libc
# are mutually recursive (make.rule already spells the libc part as a group for
# the same reason), and getting the order right by hand would be a guess that
# breaks the next time a symbol moves.  --start-group resolves it by rescanning
# until nothing new is pulled in.
#
# The compiler driver rather than $(LD): make.rule's LD is the bare linker, which
# adds no startup files, and g++ adds crt0.o and crti.o.  EwokOS supplies its own
# entry point - libewoksys's cmain.o defines _start - so those have to be kept out
# with -nostartfiles, which is exactly what the mkspec's QMAKE_LFLAGS does.
$(DEMO_BIN): $(DEMO_OBJ) $(PLUGIN_LIB) $(PLUGIN_HDR)
	@mkdir -p $(DEMO_DIR)
	$(CXX) $(CXXFLAGS) $(DEMO_OBJ) -o $@ \
		-nostartfiles -Wl,-Ttext=100 \
		-L$(SDK_DIR)/lib -L$(QT_LIBDIR) $(LDFLAGS) \
		-Wl,--start-group \
		-lewokosqpa $(QT_LIBS) \
		$(EWOK_LIB_X) $(EWOK_LIB_GRAPH) \
		-lewoksys -lc -lgloss -lcxx \
		-Wl,--end-group

all: $(PLUGIN_LIB) $(PLUGIN_HDR) $(DEMO_BIN)

plugin: $(PLUGIN_LIB) $(PLUGIN_HDR)

demo: $(DEMO_BIN)

# Header dependencies.  -MMD wrote them next to the objects; without this, editing
# qewokoswindow.h and running make again would rebuild nothing and link the old
# objects - which looks like a change that did not take effect and is not one.
DEPS = $(PLUGIN_OBJS:.o=.d) $(DEMO_OBJ:.o=.d)
-include $(DEPS)

clean:
	rm -rf $(ARCH) $(GEN_DIR)

.PHONY: all plugin demo clean
