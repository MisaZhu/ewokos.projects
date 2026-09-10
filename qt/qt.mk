#
# Shared Qt definitions for projects/qt.  Included by the top-level Makefile,
# demo/Makefile and every apps/*/Makefile, so each of those builds standalone
# against the SDK - the same way nx11's demos build against
# $(SDK_DIR)/include/nx11 and $(SDK_DIR)/lib.
#
# The top-level Makefile's "sdk" target populates:
#
#   $(SDK_DIR)/include/qt5   the complete Qt header tree - public, private and
#                            qpa headers, the generated config headers, and
#                            the mkspec's qplatformdefs.h - with every syncqt
#                            forwarding header resolved to its real file (see
#                            install-headers.sh)
#   $(SDK_DIR)/lib           libQt5*.a / libqt*.a plus libewokosqpa.a
#   $(SDK_DIR)/qt5/bin       the moc/uic/rcc host tools
#
# Two flags here are load-bearing and easy to lose:
#
#   -D__EWOKOS__      qsystemdetection.h keys Q_OS_EWOKOS and Q_OS_UNIX off this
#                     macro, because a bare -elf toolchain predefines no OS macro
#                     of its own.  Without it Qt compiles as "unknown platform":
#                     Q_OS_UNIX goes away, qcore_unix_p.h stops declaring
#                     qt_safe_poll, and the QPA plugin fails on its first
#                     include.  Qt's own objects got it from the mkspec's
#                     EWOK_CXXFLAGS.
#
#   -DQT_NO_DEBUG     Qt was configured -release.  Q_ASSERT, the layout of a few
#                     private classes and the inline/non-inline split of several
#                     QtCore functions all differ between the two, so every
#                     consumer and libQt5*.a have to agree.
#
# (The third, -DQT_STATICPLUGIN, belongs only to the plugin's own objects and
# lives in the top-level Makefile - apps are the importer side and must NOT
# have it.)
#
QT_ROOT_DIR   := $(abspath $(dir $(lastword $(MAKEFILE_LIST))))
PROJS_ROOT_DIR = $(QT_ROOT_DIR)/..
include $(PROJS_ROOT_DIR)/make.inc

# make.inc/make.rule define no target of their own, so without this the first
# rule in the including Makefile - typically a moc pattern rule - would become
# the default goal, which make then reports as "up to date" having built
# nothing.
.DEFAULT_GOAL := all

# The generated moc_*.cpp/qrc_*.cpp are chain intermediates (header -> .cpp
# -> .o), and make deletes intermediates after use - which forces the whole
# moc+compile+link chain to rerun on the next build.  Each Makefile lists its
# own generated sources under .SECONDARY to keep them on disk.  Deliberately
# NOT the blanket ".SECONDARY:" - that marks every target secondary, and make
# treats secondary targets as intermediates: a missing binary whose source
# chain carries no newer timestamp is then silently "up to date" and never
# relinked ("Nothing to be done"), which is exactly the wrong behaviour for
# the final executables.

QT_VER  = 5.15.19

QT5_INC = $(SDK_DIR)/include/qt5
QT5_BIN = $(SDK_DIR)/qt5/bin

QT_MOC  = $(QT5_BIN)/moc
QT_UIC  = $(QT5_BIN)/uic
QT_RCC  = $(QT5_BIN)/rcc

QT_DEFS = \
	-D__EWOKOS__ \
	-DQT_NO_DEBUG \
	-DQT_CORE_LIB \
	-DQT_GUI_LIB \
	-DQT_WIDGETS_LIB \
	-DQT_XML_LIB \
	-DQT_FONTDATABASE_SUPPORT_LIB

# Four levels per module, and all four are needed:
#   qt5/                       <QtCore/qdebug.h>
#   qt5/QtCore/                "qglobal.h" from qplatformdefs.h
#   qt5/<Mod>/<ver>/           <QtCore/private/qtimerinfo_unix_p.h>
#   qt5/<Mod>/<ver>/<Mod>/     <qpa/qplatformwindow.h>
# The mkspec dir is on the path even though nothing names it directly:
# QtCore/private/qcore_unix_p.h includes qplatformdefs.h by that bare name.
QT_INCS = \
	-I$(QT5_INC)/mkspecs/ewokos-g++ \
	-I$(QT5_INC) \
	-I$(QT5_INC)/QtCore \
	-I$(QT5_INC)/QtCore/$(QT_VER) \
	-I$(QT5_INC)/QtCore/$(QT_VER)/QtCore \
	-I$(QT5_INC)/QtGui \
	-I$(QT5_INC)/QtGui/$(QT_VER) \
	-I$(QT5_INC)/QtGui/$(QT_VER)/QtGui \
	-I$(QT5_INC)/QtWidgets \
	-I$(QT5_INC)/QtWidgets/$(QT_VER) \
	-I$(QT5_INC)/QtWidgets/$(QT_VER)/QtWidgets \
	-I$(QT5_INC)/QtXml \
	-I$(QT5_INC)/QtFontDatabaseSupport/$(QT_VER)

# QtXml gets one level, not four.  <QtXml/qtxmlglobal.h> and the generated
# <QtXml/qtxml-config.h> already resolve through the bare qt5/ entry above;
# qt5/QtXml/ is here for the class forwarders - <QDomDocument>, <QDomElement> -
# which is all an app ever spells.  The two versioned levels are skipped because
# nothing outside the Qt build itself includes <QtXml/private/...>: qdom_p.h and
# qxml_p.h are consumed by qdom.cpp and qxml.cpp, which are compiled inside
# qtbase-5.15, not here.  Adding them would be a path that can never be taken.
#
# QDom is in the shared list rather than simulide's own Makefile because it is
# a module of the SDK like the other three, and an app that wants <QDomDocument>
# should not have to rediscover the -I.  The link side is free for the same
# reason: libQt5Xml.a is inside the --start-group below, so an app that never
# references a QDom symbol pulls in none of its objects.

# -Wextra and -pedantic are dropped for the same reason the mkspec drops them:
# Qt 5.15's own private headers are not clean under them at this GCC version,
# and the noise buries the errors in the files that are actually ours.  -Wall
# stays.
CXXFLAGS := $(filter-out -Wextra -pedantic,$(CXXFLAGS))
CXXFLAGS += $(QT_DEFS) $(QT_INCS) -Wno-error -MMD -MP

QT_LIBS = \
	-lQt5Widgets -lQt5Gui \
	-lQt5FontDatabaseSupport -lQt5EventDispatcherSupport \
	-lQt5ThemeSupport -lQt5AccessibilitySupport -lQt5ServiceSupport \
	-lQt5DeviceDiscoverySupport -lQt5EdidSupport -lQt5FbSupport \
	-lQt5Xml -lQt5Core -lqtharfbuzz -lqtlibpng -lqtlibjpeg -lqtpcre2

# One group rather than a careful order: QtCore, QtGui, libx, libewoksys and
# libc are mutually recursive (make.rule already spells the libc part as a
# group for the same reason), and getting the order right by hand would be a
# guess that breaks the next time a symbol moves.  --start-group resolves it
# by rescanning until nothing new is pulled in.
#
# Link with the compiler driver rather than $(LD): make.rule's LD is the bare
# linker, which adds no startup files, and g++ adds crt0.o and crti.o.  EwokOS
# supplies its own entry point - libewoksys's cmain.o defines _start - so those
# have to be kept out with -nostartfiles, which is exactly what the mkspec's
# QMAKE_LFLAGS does.  $(LDFLAGS) carries -L$(SDK_DIR)/lib from make.inc.
QT_LDFLAGS = \
	-nostartfiles -Wl,-Ttext=100 $(LDFLAGS) \
	-Wl,--start-group \
	-lewokosqpa $(QT_LIBS) \
	$(EWOK_LIB_X) $(EWOK_LIB_GRAPH) \
	-lewoksys -lc -lgloss -lcxx \
	-Wl,--end-group

# SDK probe, evaluated at parse time.  moc is installed by the top-level
# Makefile's sdk target, so it doubles as the "Qt SDK is installed" marker;
# demo/ and apps/* skip themselves when it is absent instead of diving into
# compiles that fail on the first missing Qt header.
QT_SDK_MISSING := $(if $(wildcard $(QT_MOC)),,yes)
