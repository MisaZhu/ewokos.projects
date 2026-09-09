#
# Qt 5.15 on EwokOS: the "ewokos" QPA platform plugin, a Widgets demo that
# links it, and the ported apps under apps/.
#
# This builds against the tree projects/qt/build.sh produced - configure plus
# make in build/qtbuild-$(ARCH)-$(HW), with the sources in build/qtbase-5.15.
# Run build.sh first; nothing here can work without it.
#
# The plugin is deliberately NOT a qmake project.  qmake would want to run
# against an installed Qt (a qt5 .pri, a module list, a working qmake for the
# target), and the value of building it by hand is that it lives in this repo,
# under version control, next to the mkspec and the patches it depends on - so
# changing it does not mean re-running configure or touching qtbase-5.15,
# which is a vendored snapshot.
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
QT_SRC    = $(CURDIR)/qtbase-5.15
QT_BUILD  = $(CURDIR)/build/qtbuild-$(ARCH)-$(HW)
# Two include roots, both needed.  syncqt writes the forwarding headers -
# the public ones and the versioned private set, including qpa/ - into the
# source tree; the build tree only gets the generated configuration headers
# (qconfig.h, qt<module>-config.h, the Depends files).
QT_INC     = $(QT_BUILD)/include
QT_SRC_INC = $(QT_SRC)/include
QT_LIBDIR = $(QT_BUILD)/lib
QT_MOC    = $(QT_BUILD)/bin/moc
QT_UIC    = $(QT_BUILD)/bin/uic
QT_RCC    = $(QT_BUILD)/bin/rcc

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
#   include/QtCore/               (per-module dir, kept for symmetry)
#   include/<Mod>/<ver>/          <QtCore/private/qtimerinfo_unix_p.h>
#   include/<Mod>/<ver>/<Mod>/    <qpa/qplatformwindow.h>
# qtcore-config.h and friends are NOT here: qglobal.h includes them as
# <QtCore/qtcore-config.h>, which resolves against $(QT_INC) above.
# The versioned levels are listed for the build tree as well: the five
# generated private config headers (qconfig_p.h, qt*-config_p.h) live
# there and nowhere else.
QT_INCS = \
	-I$(QT_MKSPEC) \
	-I$(QT_INC) \
	-I$(QT_SRC_INC) \
	-I$(QT_SRC_INC)/QtCore \
	-I$(QT_INC)/QtCore/$(QT_VER) \
	-I$(QT_SRC_INC)/QtCore/$(QT_VER) \
	-I$(QT_INC)/QtCore/$(QT_VER)/QtCore \
	-I$(QT_SRC_INC)/QtCore/$(QT_VER)/QtCore \
	-I$(QT_SRC_INC)/QtGui \
	-I$(QT_INC)/QtGui/$(QT_VER) \
	-I$(QT_SRC_INC)/QtGui/$(QT_VER) \
	-I$(QT_INC)/QtGui/$(QT_VER)/QtGui \
	-I$(QT_SRC_INC)/QtGui/$(QT_VER)/QtGui \
	-I$(QT_SRC_INC)/QtWidgets \
	-I$(QT_INC)/QtWidgets/$(QT_VER) \
	-I$(QT_SRC_INC)/QtWidgets/$(QT_VER) \
	-I$(QT_SRC_INC)/QtWidgets/$(QT_VER)/QtWidgets \
	-I$(QT_SRC_INC)/QtFontDatabaseSupport/$(QT_VER)

# -Wextra and -pedantic are dropped for the same reason the mkspec drops them:
# Qt 5.15's own private headers are not clean under them at this GCC version, and
# the noise buries the errors in the files that are actually ours.  -Wall stays.
CXXFLAGS := $(filter-out -Wextra -pedantic,$(CXXFLAGS))
CXXFLAGS += $(QT_DEFS) $(QT_INCS) -Wno-error -MMD -MP

# Qt-tree probe, evaluated at parse time.  bin/moc is one of the first
# binaries a qtbase build produces, so it doubles as the "Qt is built" marker.
# A probe rather than either of the obvious alternatives:
#   - $(error) would also fire for `make clean` at the projects level on any
#     tree that has not built Qt;
#   - a guard rule for $(QT_MOC) never fires, because nothing lists it as a
#     prerequisite - the .o pattern rule comes from make.rule and does not
#     know about moc, so the build dived straight into compiling and failed
#     on the first missing Qt header.
QT_MISSING := $(if $(wildcard $(QT_MOC)),,yes)

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

# ---- apps: xfilemanager -----------------------------------------------------
#
# A port of https://github.com/arshc0der/XFile_Manager (MIT, see the LICENSE in
# the source directory), kept under apps/xfilemanager.  It is the first app
# here that needs uic and rcc in addition to moc; all three host tools come
# from the Qt build tree, and the generated sources live in their own gen/
# subdirectory for the same reason the plugin's and demo's do.

XFM      = xfilemanager
XFM_SRC  = apps/$(XFM)
XFM_GEN  = $(GEN_DIR)/$(XFM)
XFM_BIN  = $(SDK_DIR)/rootfs/apps/$(XFM)/$(XFM)

XFM_APP_OBJS = \
	$(ARCH)/$(XFM_SRC)/main.o \
	$(ARCH)/$(XFM_SRC)/mainwindow.o \
	$(ARCH)/$(XFM_SRC)/about_xfile_manager.o

XFM_OBJS = \
	$(XFM_APP_OBJS) \
	$(XFM_GEN)/moc_mainwindow.o \
	$(XFM_GEN)/moc_about_xfile_manager.o \
	$(XFM_GEN)/qrc_XFile_Manager_Resources.o

# uic resolves no paths of its own, so it can run from here.
$(XFM_GEN)/ui_%.h: $(XFM_SRC)/%.ui
	@mkdir -p $(XFM_GEN)
	$(QT_UIC) $< -o $@

# moc and rcc, on the other hand, run from the source directory and are handed
# the source's bare name plus an absolute output path: moc emits the name as
# the #include in the generated file (found at compile time via -I$(XFM_SRC)),
# and rcc resolves the qrc's relative <file> entries against the current
# directory.  The icons are real prerequisites of the qrc rule because rcc
# embeds their contents.
$(XFM_GEN)/moc_%.cpp: $(XFM_SRC)/%.h
	@mkdir -p $(XFM_GEN)
	cd $(XFM_SRC) && $(QT_MOC) $(QT_DEFS) $(QT_INCS) $(notdir $<) -o $(CURDIR)/$@

$(XFM_GEN)/qrc_%.cpp: $(XFM_SRC)/%.qrc $(wildcard $(XFM_SRC)/icons/*)
	@mkdir -p $(XFM_GEN)
	cd $(XFM_SRC) && $(QT_RCC) --name $* $(notdir $<) -o $(CURDIR)/$@

$(XFM_GEN)/%.o: $(XFM_GEN)/%.cpp
	$(CXX) $(CXXFLAGS) -I$(XFM_SRC) -I$(XFM_GEN) -c $< -o $@

# The generated ui_*.h must exist before the objects that include them; the
# app objects additionally search their own source dir (each other's headers)
# and the gen dir.
$(ARCH)/$(XFM_SRC)/mainwindow.o:            $(XFM_GEN)/ui_mainwindow.h
$(ARCH)/$(XFM_SRC)/about_xfile_manager.o:  $(XFM_GEN)/ui_about_xfile_manager.h
$(XFM_APP_OBJS): CXXFLAGS += -I$(XFM_SRC) -I$(XFM_GEN)

# Same link shape as the demo - see its comment for why --start-group and
# -nostartfiles look the way they do.
$(XFM_BIN): $(XFM_OBJS) $(PLUGIN_LIB) $(PLUGIN_HDR)
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) $(XFM_OBJS) -o $@ \
		-nostartfiles -Wl,-Ttext=100 \
		-L$(SDK_DIR)/lib -L$(QT_LIBDIR) $(LDFLAGS) \
		-Wl,--start-group \
		-lewokosqpa $(QT_LIBS) \
		$(EWOK_LIB_X) $(EWOK_LIB_GRAPH) \
		-lewoksys -lc -lgloss -lcxx \
		-Wl,--end-group

ifeq ($(QT_MISSING),yes)
# Skip rather than fail: the image simply ships without qtdemo and the apps,
# and everything here self-enables the next build after build.sh has run.
all plugin demo xfilemanager:
	@echo "note: projects/qt skipped - Qt 5.15 tree not found ($(QT_MOC) missing)."
	@echo "      Run projects/qt/build.sh first to configure and build Qt."
else
all: $(PLUGIN_LIB) $(PLUGIN_HDR) $(DEMO_BIN) $(XFM_BIN)

plugin: $(PLUGIN_LIB) $(PLUGIN_HDR)

demo: $(DEMO_BIN)

xfilemanager: $(XFM_BIN)
endif

# Header dependencies.  -MMD wrote them next to the objects; without this, editing
# qewokoswindow.h and running make again would rebuild nothing and link the old
# objects - which looks like a change that did not take effect and is not one.
DEPS = $(PLUGIN_OBJS:.o=.d) $(DEMO_OBJ:.o=.d) $(XFM_OBJS:.o=.d)
-include $(DEPS)

clean:
	rm -rf $(ARCH) $(GEN_DIR)

.PHONY: all plugin demo xfilemanager clean
