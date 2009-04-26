# =============================================================================
# Makefile for SpeccyBoot
# Patrik Persson, 2009
#
# Part of the SpeccyBoot project <http://speccyboot.sourceforge.net>
# =============================================================================


OBJCOPY     = objcopy
CC          = sdcc
AS          = as-z80
ECHO        = @/bin/echo

CFLAGS      = --std-sdcc99 -mz80 --Werror -I$(INCLUDEDIR)
LDFLAGS     = --out-fmt-ihx --no-std-crt0 --code-loc 0x0048 --data-loc 0x5B00
LDFLAGS_RAM = $(LDFLAGS) --code-loc 0x8100 --data-loc 0xc000

# SDCC/Z80 doesn't define bool due to incomplete support. Works for me, though.
CFLAGS     += -Dbool=BOOL


# =============================================================================
# COMPONENTS
# =============================================================================

# 256x64 mono bitmap for splash image
SPLASH_XBM   = speccyboot.xbm

# Auto-generated data for splash image (from SPLASH_XBM)
SPLASH_C     = tmp-splash-image.c

# C modules (module = source + header)
MODULES      = spectrum netboot
MODULES     += eth enc28j60_spi ip arp icmp udp dhcp tftp

# DEBUG/LOGGING BUILD (enable by 'make LOGGING=yes')
LOGGING			 = yes
ifdef LOGGING
CFLAGS		  += -DVERBOSE_LOGGING
MODULES		  += logging
endif

OTHER_HFILES = speccyboot.h
OTHER_CFILES = main.c $(SPLASH_C)

CFILES       = $(MODULES:%=%.c) $(OTHER_CFILES)
HFILES       = $(MODULES:%=%.h) $(OTHER_HFILES)
OFILES       = crt0.o $(CFILES:.c=.o)

OFILES_RAM   = $(OFILES:crt0.o=crt0_ramimage.o)

# -----------------------------------------------------------------------------

# EXE:       binary file to be loaded into FRAM (address 0x0000)
# WAV:       .wav file of EXE, to be loaded by audio interface on real machine
# Z80:       .z80 file to be tested in RAM on an emulator (address 0x8000)

EXE          = speccyboot
WAV          = $(EXE).wav
Z80          = speccyboot-ramimage.z80


# =============================================================================
# DIRECTORIES
# =============================================================================

SRCDIR       = src
INCLUDEDIR   = include
TOOLSDIR     = build-tools
IMGDIR			 = img
OBJDIR       = obj

VPATH        = $(OBJDIR)

vpath %.c    $(SRCDIR)
vpath %.asm  $(SRCDIR)
vpath %.h    $(INCLUDEDIR)
vpath %.xbm  $(IMGDIR)


# =============================================================================
# COMMAND-LINE TARGETS
# =============================================================================

all: $(EXE) $(WAV) $(Z80)

clean:
	rm -rf $(OBJDIR) $(AUTOGENDIR) $(EXE) $(WAV) $(Z80)

.SUFFIXES:

.PHONY: clean


# =============================================================================
# BUILD TOOLS (bin2wav, xbm2speccy)
# =============================================================================

BIN2WAV     = $(OBJDIR)/bin2wav
XBM2SPECCY  = $(OBJDIR)/xbm2speccy

HOSTCC      = gcc

# -----------------------------------------------------------------------------

$(BIN2WAV): $(TOOLSDIR)/bin2wav.c
	$(HOSTCC) $< -o $@

$(XBM2SPECCY): $(TOOLSDIR)/xbm2speccy.c $(SPLASH_XBM)
	$(HOSTCC) -DXBM_SOURCEFILE=\"../$(IMGDIR)/$(SPLASH_XBM)\" $< -o $@

$(OBJDIR)/$(SPLASH_C): $(XBM2SPECCY)
	$(XBM2SPECCY) > $@

%.wav: % $(BIN2WAV)
	$(BIN2WAV) $< $@


# =============================================================================
# CROSS-COMPILATION TARGETS
# =============================================================================

# SPLASH_C seems to need a special target for Make to find it
$(SPLASH_C:%.c=%.o): $(OBJDIR)/$(SPLASH_C) $(OBJDIR)
	$(CC) $(CFLAGS) -c -o $(OBJDIR)/$@ $<

%.o: %.c $(OBJDIR) $(HFILES)
	$(CC) $(CFLAGS) -c -o $(OBJDIR)/$@ $<

%.o: %.asm $(OBJDIR)
	$(AS) -o $(OBJDIR)/$@ $<

$(OBJDIR):
	mkdir -p $@

$(EXE): $(OFILES)
	$(CC) $(LDFLAGS) -o $(OBJDIR)/$(EXE) $(OFILES:%=$(OBJDIR)/%)
	$(OBJCOPY) -I ihex -O binary $(OBJDIR)/$(EXE).ihx $@

# Generate a .z80 file. Crude, but it works.
$(Z80): $(OFILES_RAM)
	$(CC) $(LDFLAGS_RAM) -o $(OBJDIR)/tmp0 $(OFILES_RAM:%=$(OBJDIR)/%)
	$(OBJCOPY) -I ihex -O binary $(OBJDIR)/tmp0.ihx $(OBJDIR)/tmp1
	$(ECHO) Generating .z80 file...
	@printf "afcblh\000\200\377\377"	 >  $(OBJDIR)/tmp2
	@dd if=/dev/zero bs=1 count=16404 >> $(OBJDIR)/tmp2 2>/dev/null
	@cat $(OBJDIR)/tmp1 >> $(OBJDIR)/tmp2
	@dd if=/dev/zero bs=1 count=32768 >> $(OBJDIR)/tmp2 2>/dev/null
	@dd if=$(OBJDIR)/tmp2 of=$@ bs=1 count=49182 2>/dev/null
