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
	@# wlink force-uppercases the NE resident/non-resident name table
	@# strings (module name and "option description") with no directive
	@# to disable it - confirmed by reading wlink's own source
	@# (ResNonResNameTable() in bld/wl/c/loados2.c unconditionally passes
	@# ucase=true to WriteLoadU8Name()). Restore the intended mixed-case
	@# text by patching the linked .exe in place: same-length swap
	@# (WIN31SS -> Win31SS, 7 bytes either way), so no other NE offsets
	@# shift. Offsets are found dynamically rather than hardcoded, since
	@# they depend on the exact object/resource layout.
	for off in $$(grep -a -b -o 'WIN31SS' $(EXE) | cut -d: -f1); do \
		printf 'Win31SS' | dd of=$(EXE) bs=1 seek=$$off count=7 conv=notrunc status=none; \
	done

$(OBJ): $(SRC) $(HDRS)
	$(WCC) $(CFLAGS) $(SRC)

$(RES): $(RCFILE) $(HDRS)
	$(WRC) -bt=windows -r -q -fo=$(RES) -I$(WATCOM)/h/win $(RCFILE)

# Windows 3.1's Control Panel Desktop applet only lists a .SCR if its NE
# non-resident-name-table entry (the module "description") starts with
# "SCRNSAVE :" - this is what distinguishes a real screensaver from any
# other renamed .EXE, and it's undocumented outside old KB articles. The
# "option description" directive must come after "system windows" and
# "name" or wlink silently ignores it; wlink also forces this field to
# uppercase (unlike the original MS LINK.EXE, which preserved case - hence
# stock screensavers showing mixed-case names like "Flying Windows" while
# ours reads "WIN31SS" in all caps. Cosmetic only; the leading "SCRNSAVE :"
# marker is what Control Panel actually checks for.
$(LNK): Makefile
	echo "debug all"                                > $(LNK)
	echo "op map, quiet"                            >> $(LNK)
	echo "system windows"                            >> $(LNK)
	echo "name win31ss"                              >> $(LNK)
	echo "option description 'SCRNSAVE : Win31SS'"   >> $(LNK)
	echo "file $(OBJ)"                               >> $(LNK)

clean:
	rm -f $(OBJ) $(RES) $(EXE) $(LNK) $(TARGET) win31ss.map
