#
# Qt 5.15 on EwokOS.  This Makefile does three things, in order:
#
#   sdk      install the complete Qt header tree, the static libraries and
#            the moc/uic/rcc host tools into $(SDK_DIR) - the same shape
#            nx11 uses ($(SDK_DIR)/include/<name> + $(SDK_DIR)/lib), see
#            qt.mk for the exact layout
#   plugin   build the "ewokos" QPA platform plugin against that SDK and
#            install libewokosqpa.a + <qt/ewokosqt.h> next to it
#   subs     recurse into demo/ and apps/*, each of which carries its own
#            standalone Makefile and links its executable against the SDK
#
# This builds against the tree projects/qt/build.sh produced - configure plus
# make in build/qtbuild-$(ARCH)-$(HW), with the sources in qtbase-5.15.
# Run build.sh first; nothing here can work without it.
#
# The plugin is deliberately NOT a qmake project.  qmake would want to run
# against an installed Qt (a qt5 .pri, a module list, a working qmake for the
# target), and the value of building it by hand is that it lives in this repo,
# under version control, next to the mkspec and the patches it depends on - so
# changing it does not mean re-running configure or touching qtbase-5.15,
# which is a vendored snapshot.
#
# The Qt compile/link flags shared with the apps (and the two load-bearing
# defines -D__EWOKOS__ / -DQT_NO_DEBUG) live in qt.mk.  One flag is local:
#
#   -DQT_STATICPLUGIN only for the plugin's own objects.  It is what makes
#                     QT_MOC_EXPORT_PLUGIN emit the static form -
#                     qt_static_plugin_EwokosIntegrationPlugin() - instead of
#                     the extern "C" dllexport form a shared plugin needs.  The
#                     apps must NOT have it: that is the QT_PLUGIN side of the
#                     distinction, and the app is the importer.
#
include qt.mk

# ---- the Qt build ----------------------------------------------------------

QT_SRC    = $(CURDIR)/qtbase-5.15
QT_BUILD  = $(CURDIR)/build/qtbuild-$(ARCH)-$(HW)
QT_MKSPEC = $(QT_SRC)/mkspecs/ewokos-g++

# Qt-tree probe, evaluated at parse time.  bin/moc is one of the first
# binaries a qtbase build produces, so it doubles as the "Qt is built" marker.
# A probe rather than $(error): $(error) would also fire for `make clean` at
# the projects level on any tree that has not built Qt.
QT_BUILD_MOC := $(QT_BUILD)/bin/moc
QT_MISSING   := $(if $(wildcard $(QT_BUILD_MOC)),,yes)

# ---- SDK install ------------------------------------------------------------
#
# Headers: both include trees are needed - syncqt wrote the forwarding headers
# (public, versioned private and qpa) into the source tree, the build tree
# only has the generated configuration headers.  install-headers.sh merges
# them into $(SDK_DIR)/include/qt5, resolving every forwarding header to its
# real file so the installed tree stands on its own.  The mkspec's
# qplatformdefs.h rides along because QtCore/private/qcore_unix_p.h includes
# it by bare name - and it in turn includes ../common/posix/qplatformdefs.h,
# which includes ../c89/qplatformdefs.h, so those two ride along at their
# relative places to keep every include resolvable inside the SDK.  A stamp
# file rather than 2000 copy targets: the tree is reinstalled as a whole
# whenever the Qt build is newer.

QT_HDR_STAMP = $(QT5_INC)/.installed

$(QT_HDR_STAMP): $(QT_BUILD_MOC) install-headers.sh
	@echo "installing qt5 headers -> $(QT5_INC)"
	@rm -rf $(QT5_INC)
	@mkdir -p $(QT5_INC)
	@sh install-headers.sh $(QT5_INC) $(QT_SRC)/include $(QT_BUILD)/include
	@mkdir -p $(QT5_INC)/mkspecs/ewokos-g++ \
		$(QT5_INC)/mkspecs/common/posix $(QT5_INC)/mkspecs/common/c89
	@cp -p $(QT_MKSPEC)/qplatformdefs.h $(QT5_INC)/mkspecs/ewokos-g++/
	@cp -p $(QT_SRC)/mkspecs/common/posix/qplatformdefs.h $(QT5_INC)/mkspecs/common/posix/
	@cp -p $(QT_SRC)/mkspecs/common/c89/qplatformdefs.h $(QT5_INC)/mkspecs/common/c89/
	@touch $@

# Libraries: exactly the set qt.mk's QT_LIBS names (libQt5Bootstrap and the
# 12/16-bit jpeg variants are host/side artifacts nothing links).
QT_LIB_NAMES = \
	Qt5Widgets Qt5Gui \
	Qt5FontDatabaseSupport Qt5EventDispatcherSupport \
	Qt5ThemeSupport Qt5AccessibilitySupport Qt5ServiceSupport \
	Qt5DeviceDiscoverySupport Qt5EdidSupport Qt5FbSupport \
	Qt5Core qtharfbuzz qtlibpng qtlibjpeg qtpcre2

QT_SDK_LIBS = $(patsubst %,$(SDK_DIR)/lib/lib%.a,$(QT_LIB_NAMES))

$(SDK_DIR)/lib/lib%.a: $(QT_BUILD)/lib/lib%.a
	@mkdir -p $(dir $@)
	cp -p $< $@

# Host tools: moc/uic/rcc, so the app Makefiles need nothing from the Qt
# build tree.  Kept out of $(SDK_DIR)/lib and rootfs - they are host binaries.
QT_SDK_TOOLS = $(QT_MOC) $(QT_UIC) $(QT_RCC)

$(QT5_BIN)/%: $(QT_BUILD)/bin/%
	@mkdir -p $(dir $@)
	cp -p $< $@

# ---- generated sources ------------------------------------------------------
#
# Under gen/ rather than $(ARCH)/ so that make.rule's "$(ARCH)/%.o: %.cpp"
# pattern cannot also match them.  Two rules matching one target is resolved by
# which prerequisite exists, which works, but is a thing better not relied on.

GEN_DIR        = gen/$(ARCH)
MOC_PLUGIN_DIR = $(GEN_DIR)/plugin

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

# moc is handed the defines and the include path but not the compiler flags: it
# parses the sources itself and only understands -D, -I and -f, so -nostdinc++
# and friends would be rejected rather than ignored.
$(MOC_PLUGIN_DIR)/moc_%.cpp: src/platform/ewokos/%.h
	@mkdir -p $(MOC_PLUGIN_DIR)
	$(QT_MOC) $(QT_DEFS) $(QT_INCS) $< -o $@

$(MOC_PLUGIN_DIR)/%.moc: src/platform/ewokos/%.cpp
	@mkdir -p $(MOC_PLUGIN_DIR)
	$(QT_MOC) $(QT_DEFS) $(QT_INCS) $< -o $@

$(MOC_PLUGIN_DIR)/%.o: $(MOC_PLUGIN_DIR)/%.cpp
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) -DQT_STATICPLUGIN -I$(MOC_PLUGIN_DIR) -c $< -o $@

# main.cpp #includes "main.moc"; a quoted include searches the including
# file's own directory first and then -I in order, so the generated dir has
# to be on the path.  QT_STATICPLUGIN is the plugin's and nobody else's.
$(PLUGIN_OBJS): CXXFLAGS += -DQT_STATICPLUGIN -I$(MOC_PLUGIN_DIR)

$(ARCH)/src/platform/ewokos/main.o: $(MOC_PLUGIN_DIR)/main.moc
$(ARCH)/src/platform/ewokos/qewokoseventdispatcher.o: $(MOC_PLUGIN_DIR)/moc_qewokoseventdispatcher.cpp

# ar appends, so a stale archive would keep the previous build's objects next to
# the new ones and link whichever satisfied a symbol first.
$(PLUGIN_LIB): $(PLUGIN_OBJS)
	@mkdir -p $(dir $@)
	rm -f $@
	$(AR) cqs $@ $(PLUGIN_OBJS)

$(PLUGIN_HDR): include/qt/ewokosqt.h
	@mkdir -p $(dir $@)
	cp $< $@

# ---- demo and apps ----------------------------------------------------------

SUB_DIRS = \
	demo \
	apps/pcmanfm-qt \
	apps/qterminal \
	apps/nomacs \
	apps/freecad \
	apps/qtmesheditor \
	apps/notepadqq

ifeq ($(QT_MISSING),yes)
# Skip rather than fail: the image simply ships without qtdemo and the apps,
# and everything here self-enables the next build after build.sh has run.
all sdk plugin demo pcmanfm qterminal nomacs freecad qtmesheditor notepadqq:
	@echo "note: projects/qt skipped - Qt 5.15 tree not found ($(QT_BUILD_MOC) missing)."
	@echo "      Run projects/qt/build.sh first to configure and build Qt."
else

# Sequenced through recursive make on purpose: the plugin's moc rules use the
# SDK moc and the sub-Makefiles compile against the SDK headers, so neither
# may start before the sdk phase has finished writing them.
all: sdk
	@$(MAKE) plugin-lib
	@for dir in $(SUB_DIRS); do $(MAKE) -C $$dir || exit 1; done
	@echo "qt done."

sdk: $(QT_HDR_STAMP) $(QT_SDK_LIBS) $(QT_SDK_TOOLS)

plugin: sdk
	@$(MAKE) plugin-lib

plugin-lib: $(PLUGIN_LIB) $(PLUGIN_HDR)

demo: plugin
	@$(MAKE) -C demo

pcmanfm: plugin
	@$(MAKE) -C apps/pcmanfm-qt

qterminal: plugin
	@$(MAKE) -C apps/qterminal

nomacs: plugin
	@$(MAKE) -C apps/nomacs

freecad: plugin
	@$(MAKE) -C apps/freecad

qtmesheditor: plugin
	@$(MAKE) -C apps/qtmesheditor

notepadqq: plugin
	@$(MAKE) -C apps/notepadqq

endif

# Header dependencies.  -MMD wrote them next to the objects; without this,
# editing qewokoswindow.h and running make again would rebuild nothing and
# link the old objects - which looks like a change that did not take effect
# and is not one.
-include $(PLUGIN_OBJS:.o=.d)

clean:
	rm -rf $(ARCH) $(GEN_DIR)
	rm -f $(PLUGIN_LIB) $(PLUGIN_HDR) $(QT_SDK_LIBS)
	rm -rf $(QT5_INC) $(SDK_DIR)/qt5
	@for dir in $(SUB_DIRS); do $(MAKE) -C $$dir clean; done

.PHONY: all sdk plugin plugin-lib demo pcmanfm qterminal nomacs freecad qtmesheditor notepadqq clean
