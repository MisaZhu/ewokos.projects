#
# qtdemo - the QtWidgets bring-up demo for the "ewokos" QPA plugin.
#
# Standalone: builds against the SDK that `make sdk plugin` in projects/qt
# installed (headers in $(SDK_DIR)/include/qt5, libQt5*.a + libewokosqpa.a in
# $(SDK_DIR)/lib, moc in $(SDK_DIR)/qt5/bin).
#
include ../qt.mk

DEMO     = qtdemo
DEMO_DIR = $(SDK_DIR)/rootfs/apps/$(DEMO)
DEMO_BIN = $(DEMO_DIR)/$(DEMO)

GEN = gen/$(ARCH)

DEMO_OBJ = $(ARCH)/main.o

# main.cpp #includes "main.moc"; a quoted include searches the including
# file's own directory first and then -I in order, so the generated dir has
# to be on the path.
CXXFLAGS += -I$(GEN)

$(GEN)/%.moc: %.cpp
	@mkdir -p $(GEN)
	$(QT_MOC) $(QT_DEFS) $(QT_INCS) $< -o $@

$(DEMO_OBJ): $(GEN)/main.moc

ifeq ($(QT_SDK_MISSING),yes)
all:
	@echo "note: qtdemo skipped - Qt SDK not installed ($(QT_MOC) missing)."
	@echo "      Run make in projects/qt first."
else
all: $(DEMO_BIN)
endif

$(DEMO_BIN): $(DEMO_OBJ)
	@mkdir -p $(DEMO_DIR)
	$(CXX) $(CXXFLAGS) $(DEMO_OBJ) -o $@ $(QT_LDFLAGS)
	cp -r res $(DEMO_DIR)

-include $(DEMO_OBJ:.o=.d)

clean:
	rm -rf $(ARCH) gen

.PHONY: all clean
