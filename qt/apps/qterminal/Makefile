#
# qterminal - a port of https://github.com/lxqt/qterminal (GPL-2.0) plus its
# widget library https://github.com/lxqt/qtermwidget, both vendored at 1.4.0 -
# the last Qt5-based releases.  Real source-level port, unlike the sibling
# apps: the only rewritten piece is qtermwidget/Pty.cpp, which hosts
# /bin/shell over pipe pairs the way sshd does (EwokOS has no /dev/ptmx and
# Qt was built -no-feature-process).  DBus stays off (HAVE_QDBUS undefined),
# the X11 global hotkey is a stub header, translations are not shipped - but
# TRANSLATIONS_DIR must still be defined because qtermwidget.cpp names it
# unconditionally.
#
# Standalone: builds against the SDK that `make sdk plugin` in projects/qt
# installed (see ../../qt.mk for the layout).
#
include ../../qt.mk

APP = qterminal
BIN = $(SDK_DIR)/rootfs/apps/$(APP)/$(APP)

SRC = src
TW  = qtermwidget
GEN = gen/$(ARCH)

# kb-layouts and color-schemes are data files qtermwidget reads at runtime;
# the link rule copies res/ to the rootfs dir these point at.
QTERM_DEFS = \
	-DQTERMINAL_VERSION='"1.4.0"' \
	-DKB_LAYOUT_DIR='"/apps/qterminal/res/kb-layouts"' \
	-DCOLORSCHEMES_DIR='"/apps/qterminal/res/color-schemes"' \
	-DTRANSLATIONS_DIR='"/apps/qterminal/res/translations"'

CXXFLAGS += $(QTERM_DEFS) \
	-I$(SRC) \
	-I$(SRC)/third-party \
	-I$(TW) \
	-I$(GEN)/src \
	-I$(GEN)/tw

APP_OBJS = $(patsubst %,$(ARCH)/$(SRC)/%.o, \
	main mainwindow tabwidget tabbar termwidget termwidgetholder \
	properties propertiesdialog bookmarkswidget fontdialog \
	qterminalutils tab-switcher terminalconfig dbusaddressable)

TW_OBJS = $(patsubst %,$(ARCH)/$(TW)/%.o, \
	BlockArray ColorScheme Emulation Filter History HistorySearch \
	KeyboardTranslator konsole_wcwidth Pty Screen ScreenWindow SearchBar \
	Session ShellCommand TerminalCharacterDecoder TerminalDisplay \
	Vt102Emulation tools qtermwidget)

# Every Q_OBJECT header, compiled as separate moc_*.cpp - none of the vendored
# sources #include their own .moc.  Two gen subdirs (src/ and tw/) so the two
# moc pattern rules cannot claim each other's outputs.
APP_MOC_OBJS = $(patsubst %,$(GEN)/src/moc_%.o, \
	bookmarkswidget fontdialog mainwindow propertiesdialog qterminalapp \
	tab-switcher tabbar tabwidget termwidget termwidgetholder \
	qxtglobalshortcut)

TW_MOC_OBJS = $(patsubst %,$(GEN)/tw/moc_%.o, \
	Emulation Filter HistorySearch Pty ScreenWindow SearchBar Session \
	TerminalDisplay Vt102Emulation qtermwidget)

UI_HDRS = \
	$(patsubst %,$(GEN)/src/ui_%.h, \
		bookmarkswidget fontdialog propertiesdialog qterminal) \
	$(GEN)/tw/ui_SearchBar.h

OBJS = \
	$(APP_OBJS) $(APP_MOC_OBJS) \
	$(TW_OBJS) $(TW_MOC_OBJS) \
	$(GEN)/src/qrc_icons.o

# The moc_*.cpp/qrc_*.cpp are chain intermediates (header/.qrc -> .cpp ->
# .o); keep them on disk or make reruns the whole generate+compile+link chain
# every build.  (The ui_*.h are explicit prerequisites below, so they are
# never intermediates.)
.SECONDARY: $(APP_MOC_OBJS:.o=.cpp) $(TW_MOC_OBJS:.o=.cpp) $(GEN)/src/qrc_icons.cpp

# moc gets the app defines too: HAVE_QDBUS-style conditionals inside Q_OBJECT
# classes change which slots exist, and moc has to see the same truth as g++.
# Run from inside the source dir so the generated #include names the bare
# header, which -I$(SRC)/-I$(TW) then resolve.
$(GEN)/src/moc_%.cpp: $(SRC)/%.h
	@mkdir -p $(dir $@)
	cd $(SRC) && $(QT_MOC) $(QT_DEFS) $(QTERM_DEFS) $(QT_INCS) $(notdir $<) -o $(CURDIR)/$@

$(GEN)/src/moc_%.cpp: $(SRC)/third-party/%.h
	@mkdir -p $(dir $@)
	cd $(SRC)/third-party && $(QT_MOC) $(QT_DEFS) $(QTERM_DEFS) $(QT_INCS) $(notdir $<) -o $(CURDIR)/$@

$(GEN)/tw/moc_%.cpp: $(TW)/%.h
	@mkdir -p $(dir $@)
	cd $(TW) && $(QT_MOC) $(QT_DEFS) $(QTERM_DEFS) $(QT_INCS) $(notdir $<) -o $(CURDIR)/$@

$(GEN)/src/ui_%.h: $(SRC)/forms/%.ui
	@mkdir -p $(dir $@)
	$(QT_UIC) $< -o $@

$(GEN)/tw/ui_%.h: $(TW)/%.ui
	@mkdir -p $(dir $@)
	$(QT_UIC) $< -o $@

# rcc from inside src/ so the .qrc's relative <file> entries resolve.
$(GEN)/src/qrc_%.cpp: $(SRC)/%.qrc $(wildcard $(SRC)/icons/*)
	@mkdir -p $(dir $@)
	cd $(SRC) && $(QT_RCC) --name $* $(notdir $<) -o $(CURDIR)/$@

$(GEN)/src/%.o: $(GEN)/src/%.cpp
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) -c $< -o $@

$(GEN)/tw/%.o: $(GEN)/tw/%.cpp
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) -c $< -o $@

# The ui_*.h are included from headers (propertiesdialog.h and friends), so the
# moc_*.o compiles need them just as much as the plain objects do.
$(OBJS): $(UI_HDRS)

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
