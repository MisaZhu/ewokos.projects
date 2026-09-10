#
# simulide - SimulIDE 0.4.13 (GPL-3.0, https://github.com/SimulIDE/SimulIDE)
# built for EwokOS.  See src/LICENSE-upstream for the upstream licence and
# src/mainwindow.h for what had to change to get it here.
#
# This is upstream's own source tree, not a rewrite: src/ is SimulIDE.pri's
# layout with five subtrees removed, and the .pri's INCLUDEPATH is reproduced
# below by discovery rather than by hand so that adding a component directory
# does not also mean editing this file.
#
# Removed, and why:
#
#   src/gpsim/                          the PIC instruction set simulator, 70
#                                       files of vendored C needing glib,
#                                       config.h and popt.
#   src/simavr/                         the AVR instruction set simulator, 81
#                                       files of vendored C needing libelf.
#   src/simulator/elements/processors/  AvrProcessor and PicProcessor, the two
#                                       classes that drove the trees above.
#                                       baseprocessor.h/.cpp and ramtable stay:
#                                       they are the abstract base and the RAM
#                                       watch table, and the firmware editor
#                                       below is written against them.
#   src/gui/circuitwidget/components/mcu/
#                                       AVRComponent, PICComponent, Arduino,
#                                       SerialPort and SerialTerm.  mcucomponent.h
#                                       stays, as a stub, because ten files that
#                                       were kept still ask McuComponent whether
#                                       one exists - see the note in that header.
#   src/gui/terminalwidget/terminalwidget.{h,cpp}
#                                       the MCU's UART monitor.  Its sibling
#                                       outpaneltext.{h,cpp} stays: the editor
#                                       takes an OutPanelText* in every
#                                       constructor.
#
# src/gui/editorwidget/ is NOT removed, though an earlier revision of this file
# said it was.  It is 27 files - code editor, syntax highlighter, find/replace
# dialog and five assembler debuggers - and none of them includes anything from
# gpsim, simavr or the processors subtree; what they reach for is baseprocessor.h,
# mcucomponent.h, ramtable.h and outpaneltext.h, all of which are present.  They
# are restored verbatim, and the two things they need that this Qt does not have
# are supplied rather than edited around: see src/compat/qprocess_shim.h.
#
# Worked around, each noted where it is done:
#
#   Qt5Xml         solved at the source.  QDomDocument in circuit/chip/
#                  subcircuit/Property/componentselector is upstream's own code,
#                  unmodified: ../../build.sh no longer passes -no-feature-xml,
#                  so libQt5Xml.a is built and installed and -lQt5Xml is in
#                  ../../qt.mk.
#   Qt5Concurrent  simulator.cpp's QtConcurrent::run for the step loop, replaced
#                  by running the step in the calling thread.
#   QProcess       src/compat/qprocess_shim.h, injected with -include below.
#   Qt5Multimedia  outputs/audio_out.
#   Qt5Svg         circuitview.cpp's SVG export.
#   Qt5Script      elements/logic/e-function's expression evaluation.
#
# Standalone: builds against the SDK that `make sdk plugin` in projects/qt
# installed (see ../../qt.mk for the layout).
#
include ../../qt.mk

APP  = simulide
BIN  = $(SDK_DIR)/rootfs/apps/$(APP)/$(APP)
SRCD = src

# Everything generated - objects, moc, rcc - under build/, which projects/
# .gitignore already excludes via build*.  The legacy layout put objects in
# $(ARCH)/ and generated sources in gen/$(ARCH)/, and the .d files that -MMD
# drops next to them were not covered by any ignore rule.
BLD  = build/$(ARCH)
MOCD = $(BLD)/moc

# ---- flags ------------------------------------------------------------------
#
# SimulIDE.pro's DEFINES.  MAINMODULE_EXPORT is defined empty there too: it is
# a dllexport marker for a shared-library build that upstream does not actually
# do, and every gui header spells it in front of its class.
#
# APP_VERSION is what the About box prints and what Circuit writes into a
# .simu.  Upstream's is "0.4.13-SR5-CE" from VERSION+RELEASE in the .pro; the
# suffix here says which tree it came from and that it is this port.
SIMU_DEFS  = -DMAINMODULE_EXPORT=
SIMU_DEFS += -DAPP_VERSION='"0.4.13-ewok"'

# Where the shipped data lands in the rootfs.  Upstream derives these at run
# time from the executable's path (simuapi_apppath.cpp) and expects
# <appdir>/../share/simulide/{data,examples}; EwokOS installs an app as
# /apps/<name>/ with its resources underneath, so the two paths are given to
# the binary instead of being derived.  See simuapi_apppath.cpp.
SIMU_DEFS += -DSIMULIDE_DATA_DIR='"/apps/simulide/res/data"'
SIMU_DEFS += -DSIMULIDE_EXAMPLES_DIR='"/apps/simulide/res/examples"'

# The .pri's INCLUDEPATH, by discovery: every directory holding a header.  The
# five removed subtrees are simply not there to be found, so this cannot drift
# out of step with them the way a hand-written list would.
SIMU_INCS := $(addprefix -I,$(sort $(dir $(shell find $(SRCD) -name '*.h'))))
SIMU_INCS += -Iavr  # AVR simulator headers

# $(MOCD) is on the path because upstream's .cpp files end with
# #include "moc_<name>.cpp" and the .pri puts a single flat MOC_DIR on the
# include path for exactly that reason.  Flat is safe here: no two Q_OBJECT
# headers in the tree share a basename.
CXXFLAGS += -I$(SRCD) $(SIMU_INCS) -I$(MOCD)
CXXFLAGS += $(SIMU_DEFS)

# src/compat/qprocess_shim.h, forced into every translation unit.
#
# -include rather than an #include in the files that need it, because the point of
# the shim is that src/gui/editorwidget/ stays byte-identical to upstream - and
# upstream writes no `#include <QProcess>` anywhere.  It never had to: QProcess
# arrived through <QtGui>, which pulls in <QtCore/QtCore>, whose line 169 includes
# qprocess.h.  That line is inside `#if QT_CONFIG(processenvironment)`, which is
# false in this build, so the name simply never appears.
#
# The shim is named qprocess_shim.h and not qprocess.h on purpose.  $(QT_INCS)
# puts the SDK's include/qt5/QtCore on the path, so a file of ours called
# qprocess.h would be found by `-include qprocess.h` before ours - and it is the
# real Qt header, which opens with QT_REQUIRE_CONFIG(processenvironment), a
# Q_STATIC_ASSERT_X that this build fails.  A name nothing else can claim avoids
# the question.  For the same reason the SDK's copy is never a risk in the other
# direction: <QtCore/QtCore> spells `#include "qprocess.h"`, and a quoted include
# looks in the directory of the file doing the including first.
#
# It costs one extra header per translation unit.  The class is a QObject with
# Q_OBJECT, so it is also in the moc set below by the same discovery that finds
# every other Q_OBJECT header, and qprocess_shim.cpp ends with the matching
# #include "moc_qprocess_shim.cpp".
CXXFLAGS += -include qprocess_shim.h

# The three the .pro silences.  -Wno-missing-field-initializers and
# -Wno-implicit-fallthrough are C flags upstream that g++ also accepts, and
# -Wno-unused-parameter is already in make.rule's CXXFLAGS but is repeated
# because a later -W re-enables what an earlier one turned off.
CXXFLAGS += -Wno-unused-parameter -Wno-missing-field-initializers
CXXFLAGS += -Wno-implicit-fallthrough

# QtCore/qstdlibdetection.h ends in "#error Unknown std library implementation"
# unless one of _LIBCPP_VERSION, __GLIBCXX__, _MSVC_STL_VERSION, _YVALS,
# _STLPORT_VERSION, __SGI_STL or _RWSTD_VER is defined, and make.rule compiles
# with -nostdinc++ so that EWOK_STL is the only C++ library in scope - EWOK_STL
# defines none of them, and the aarch64-none-elf driver predefines none either
# (it has __GLIBCXX_TYPE_INT_N_0 but not __GLIBCXX__ itself).
#
# It is only reached through a module umbrella header: <QtWidgets> pulls in
# QtWidgetsDepends, which pulls in <QtCore/QtCore>, whose line 224 includes
# qstdlibdetection.h.  Seventeen files here include an umbrella header, and
# <QWidget> on its own is fine, so the alternative would be editing all
# seventeen to name the individual headers they want.
#
# Feeding the probe is safe rather than a lie that might be believed: the
# Q_STL_* macro it selects is defined and then read by nothing in QtCore,
# QtGui or QtWidgets, and the only other place either macro is tested is
# qcompilerdetection.h:1035, which is inside "#if defined(Q_OS_QNX)".
#
# The value is the one this toolchain's own libstdc++ carries (Arm GNU
# Toolchain 13.3.Rel1, aarch64-none-elf/include/c++/13.3.1/.../bits/
# c++config.h), so that if a future Qt ever does read it as a version it reads
# a true one rather than a number invented here.
CXXFLAGS += -D__GLIBCXX__=20240614

# ---- sources ----------------------------------------------------------------

SRCS := $(sort $(shell find $(SRCD) -name '*.cpp'))

# AVR simulator core (outside src/, pure C compiled as C++)
AVR_SRCS := $(sort $(wildcard avr/*.cpp))
SRCS += $(AVR_SRCS)

OBJS := $(patsubst %.cpp,$(BLD)/%.o,$(SRCS))

QRC_CPP = $(MOCD)/qrc_application.cpp

# In mocobj/ rather than at the top of $(BLD) so that it is built by the same
# rule as the standalone moc objects: that rule's prerequisite is a file in
# $(MOCD), and the generic $(BLD)/%.o rule would look for qrc_application.cpp
# in the current directory instead.
QRC_OBJ = $(BLD)/mocobj/qrc_application.o

# ---- moc --------------------------------------------------------------------
#
# Upstream mixes two moc idioms and the split has to be respected or the meta-
# objects are defined twice: 103 of the .cpp files end with #include
# "moc_<name>.cpp", which compiles the meta-object into that same translation
# unit, while 12 Q_OBJECT headers have no such include and need their moc
# compiled as an object of its own.  Which is which is read off the sources
# rather than listed, so adding a component does not mean editing this file.
MOC_HDRS  := $(sort $(shell grep -rl Q_OBJECT $(SRCD) --include='*.h'))
MOC_BASES := $(notdir $(MOC_HDRS:.h=))
MOC_ALL   := $(addsuffix .cpp,$(addprefix $(MOCD)/moc_,$(MOC_BASES)))

# One explicit rule per header rather than a single $(MOCD)/moc_%.cpp pattern
# rule, for two reasons.  A pattern rule cannot name its prerequisite here -
# the header has to be found from the stem, and the stem is only a basename -
# so it would have none, and a target with no prerequisite and no file on disk
# is rebuilt every time make is run.  Worse, a prerequisite-less pattern rule
# matches any stem at all: when make went looking for a way to rebuild one of
# the .d files named in the -include below, it matched this rule with the stem
# "filewidget.d" and ran moc with an empty input.  Explicit rules also let the
# generated file depend on the header it came from.
#
# moc gets the defines and the include path but not the compiler flags, and it
# gets SIMU_DEFS as well as QT_DEFS: a Q_OBJECT class guarded on one of them,
# or one whose declaration spells MAINMODULE_EXPORT, would otherwise produce a
# meta-object that disagrees with the compiled class.
define MOC_RULE
$(MOCD)/moc_$(notdir $(1:.h=)).cpp: $(1)
	@mkdir -p $$(dir $$@)
	$(QT_MOC) $(QT_DEFS) $(SIMU_DEFS) $(QT_INCS) -I$(SRCD) $(SIMU_INCS) $$< -o $$@

endef
$(foreach h,$(MOC_HDRS),$(eval $(call MOC_RULE,$(h))))

# "src/.../led.cpp:moc_led.cpp", one per line, from a single grep.
# The character class includes hyphen: upstream has files like clock-base.h
# whose moc is moc_clock-base.cpp, and [A-Za-z0-9_] would not match it.
MOC_PAIRS := $(shell grep -rHo 'moc_[A-Za-z0-9_-]*\.cpp' $(SRCS) 2>/dev/null)

# The moc files already compiled into someone else's object.
MOC_INLINED := $(sort $(notdir $(foreach p,$(MOC_PAIRS),$(word 2,$(subst :, ,$(p))))))

# ... and the ones that are not, which become objects of their own.
MOC_STANDALONE     := $(filter-out $(MOC_INLINED),$(notdir $(MOC_ALL)))
MOC_STANDALONE_OBJ := $(addprefix $(BLD)/mocobj/,$(MOC_STANDALONE:.cpp=.o))

# A .cpp that ends with #include "moc_x.cpp" cannot compile until that file
# exists, and nothing else in the dependency graph says so.
#
# $(patsubst) rather than the shorter $(word 1,...:.cpp=.o): a substitution
# reference applies to the variable named in front of the colon, and what is in
# front of it here is the whole $(word ...) call, so the ":.cpp=.o" lands at the
# end of word's *argument text* - attached to the second word, which word then
# discards.  The rule that comes out names a .cpp on the left instead of a .o,
# matches nothing that is ever built, and every one of the 103 inline-moc
# objects compiles before its moc file exists.
define MOC_DEP
$(BLD)/$(patsubst %.cpp,%.o,$(word 1,$(subst :, ,$(1)))): $(MOCD)/$(word 2,$(subst :, ,$(1)))
endef
$(foreach p,$(MOC_PAIRS),$(eval $(call MOC_DEP,$(p))))

GEN_SRCS = $(MOC_ALL) $(QRC_CPP)

# Generated sources are chain intermediates (header -> .cpp -> .o) and make
# deletes intermediates after use, which would rerun the whole moc+compile+link
# chain on the next build.  See qt.mk for why this is not the blanket form.
.SECONDARY: $(GEN_SRCS)

# ---- rules ------------------------------------------------------------------

ifeq ($(QT_SDK_MISSING),yes)
all:
	@echo "note: $(APP) skipped - Qt SDK not installed ($(QT_MOC) missing)."
	@echo "      Run make in projects/qt first."
else
all: $(BIN)
endif

$(BLD)/%.o: %.cpp
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) -c $< -o $@

$(BLD)/mocobj/%.o: $(MOCD)/%.cpp
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) -c $< -o $@

# rcc resolves the paths inside the .qrc relative to the .qrc's own directory,
# which is why application.qrc can stay in src/ next to src/icons/.
$(QRC_CPP): $(SRCD)/application.qrc
	@mkdir -p $(dir $@)
	$(QT_RCC) -name application $< -o $@

$(BIN): $(OBJS) $(MOC_STANDALONE_OBJ) $(QRC_OBJ)
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) $^ -o $@ $(QT_LDFLAGS)
	rsync -a --delete res $(dir $@)

# Through $(wildcard), so that make is not asked to rebuild dependency files
# that do not exist yet.  It will otherwise go looking for a rule that can
# produce them on the very first build, when none of them have been written.
-include $(wildcard $(OBJS:.o=.d))
-include $(wildcard $(MOC_STANDALONE_OBJ:.o=.d))

clean:
	rm -rf build

# What the build is actually made of, for checking the discovery above against
# the tree rather than guessing at it.
info:
	@echo "ARCH           $(ARCH)"
	@echo "sources        $(words $(SRCS))"
	@echo "objects        $(words $(OBJS))"
	@echo "moc headers    $(words $(MOC_HDRS))"
	@echo "moc inlined    $(words $(MOC_INLINED))"
	@echo "moc standalone $(words $(MOC_STANDALONE))"
	@echo "include dirs   $(words $(SIMU_INCS))"

.PHONY: all clean info
