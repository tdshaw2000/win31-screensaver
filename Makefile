# Makefile for Win31SS - a Windows 3.1 (Win16) screensaver.
# Cross-compiled from Linux using OpenWatcom 2.0, producing WIN31SS.SCR
# (a Win16 NE executable, just renamed to the .SCR extension Windows 3.1
# expects for screensavers).
#
# Mirrors the build recipe OpenWatcom ships in its own Win16 sample
# (samples/win/generic/win16 in the OpenWatcom tree): compile with wcc,
# link with wlink via a generated directive file, then bind the .res
# resources into the linked .exe with wrc.
#
# Requires the WATCOM environment variable to point at an OpenWatcom
# installation (e.g. /opt/watcom). The Dockerfile in this repo sets this
# up; for a manual install, set WATCOM yourself before running make.

WATCOM  ?= /opt/watcom
BINDIR  ?= $(WATCOM)/binl

WCC     = $(BINDIR)/wcc
WLINK   = $(BINDIR)/wlink
WRC     = $(BINDIR)/wrc

SRCDIR  = src
TARGET  = WIN31SS.SCR
EXE     = win31ss.exe
OBJ     = win31ss.obj
RES     = win31ss.res
LNK     = win31ss.lnk

SRC     = $(SRCDIR)/win31ss.c
RCFILE  = $(SRCDIR)/win31ss.rc
HDRS    = $(SRCDIR)/resource.h

# -zW      : build a Windows GUI app - generates the entry-sequence
#            thunks Windows 3.x needs to call back into WndProc/DialogProc,
#            so no module-definition EXPORTS section is required.
# -3       : target 80386 instructions - a safe baseline for a 386-class
#            Windows 3.1 machine (and 86Box's default CPU).
# -w=3     : moderate warning level.
# -e=25    : cap error reporting at 25, since this is a strict C89 build
#            (OpenWatcom's 16-bit compiler rejects modern C).
CFLAGS  = -zW -3 -w=3 -e=25 -I$(WATCOM)/h -I$(WATCOM)/h/win -fo=$(OBJ)

.PHONY: all clean

all: $(TARGET)

$(TARGET): $(EXE)
	cp $(EXE) $(TARGET)

$(EXE): $(OBJ) $(RES) $(LNK)
	$(WLINK) @$(LNK)
	$(WRC) -q $(RES) $(EXE)

$(OBJ): $(SRC) $(HDRS)
	$(WCC) $(CFLAGS) $(SRC)

$(RES): $(RCFILE) $(HDRS)
	$(WRC) -bt=windows -r -q -fo=$(RES) -I$(WATCOM)/h/win $(RCFILE)

$(LNK): Makefile
	echo "debug all"        > $(LNK)
	echo "name win31ss"     >> $(LNK)
	echo "op map, quiet"    >> $(LNK)
	echo "system windows"   >> $(LNK)
	echo "file $(OBJ)"      >> $(LNK)

clean:
	rm -f $(OBJ) $(RES) $(EXE) $(LNK) $(TARGET) win31ss.map
